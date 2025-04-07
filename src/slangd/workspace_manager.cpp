#include "slangd/workspace_manager.hpp"

#include <filesystem>

#include <slang/parsing/Lexer.h>
#include <slang/util/Bag.h>
#include <spdlog/spdlog.h>

#include "slangd/utils/source_utils.hpp"
#include "slangd/utils/timer.hpp"
#include "slangd/utils/uri.hpp"

namespace slangd {

WorkspaceManager::WorkspaceManager(
    asio::any_io_executor executor, std::shared_ptr<spdlog::logger> logger)
    : logger_(logger ? logger : spdlog::default_logger()),
      source_manager_(std::make_shared<slang::SourceManager>()),
      source_loader_(
          std::make_unique<slang::driver::SourceLoader>(*source_manager_)),
      executor_(executor),
      strand_(asio::make_strand(executor)) {
}

void WorkspaceManager::AddWorkspaceFolder(
    const std::string& uri, const std::string& name) {
  if (!IsFileUri(uri)) {
    Logger()->warn(
        "WorkspaceManager skipping non-file URI workspace folder: {}", uri);
    return;
  }

  std::string local_path = UriToPath(uri);

  if (!std::filesystem::exists(local_path)) {
    Logger()->warn(
        "WorkspaceManager skipping non-existent workspace folder: {}",
        local_path);
    return;
  }

  Logger()->debug(
      "WorkspaceManager adding workspace folder: {} ({})", name, local_path);
  workspace_folders_.push_back(local_path);
}

auto WorkspaceManager::ScanWorkspace() -> asio::awaitable<void> {
  Logger()->debug(
      "WorkspaceManager starting workspace scan for SystemVerilog files");

  std::vector<std::string> all_files;

  for (const auto& folder : workspace_folders_) {
    Logger()->debug("WorkspaceManager scanning workspace folder: {}", folder);
    auto files = co_await FindSystemVerilogFilesInDirectory(folder);
    Logger()->debug(
        "WorkspaceManager found {} SystemVerilog files in {}", files.size(),
        folder);

    // Collect all files
    all_files.insert(all_files.end(), files.begin(), files.end());
  }

  // Process all collected files together
  co_await IndexFiles(all_files);

  Logger()->debug(
      "WorkspaceManager workspace scan completed. Total indexed files: {}",
      syntax_trees_.size());

  co_return;
}

auto WorkspaceManager::HandleFileChanges(std::vector<lsp::FileEvent> changes)
    -> asio::awaitable<void> {
  Logger()->debug("WorkspaceManager handling {} file changes", changes.size());

  // Process each file change event
  for (const auto& change : changes) {
    // Convert URI to local file path
    if (!IsFileUri(change.uri)) {
      Logger()->debug("WorkspaceManager skipping non-file URI: {}", change.uri);
      continue;
    }

    std::string local_path = UriToPath(change.uri);

    // Call the appropriate handler based on change type
    switch (change.type) {
      case lsp::FileChangeType::kCreated:
        Logger()->debug("WorkspaceManager file created: {}", local_path);
        co_await HandleFileCreated(local_path);
        break;

      case lsp::FileChangeType::kChanged:
        Logger()->debug("WorkspaceManager file changed: {}", local_path);
        co_await HandleFileChanged(local_path);
        break;

      case lsp::FileChangeType::kDeleted:
        Logger()->debug("WorkspaceManager file deleted: {}", local_path);
        co_await HandleFileDeleted(local_path);
        break;
    }
  }

  // Rebuild compilation after processing all changes
  co_await RebuildWorkspaceCompilation();

  co_return;
}

auto WorkspaceManager::FindSystemVerilogFilesInDirectory(std::string directory)
    -> asio::awaitable<std::vector<std::string>> {
  std::vector<std::string> sv_files;

  Logger()->debug("WorkspaceManager scanning directory: {}", directory);

  try {
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(directory)) {
      if (entry.is_regular_file()) {
        std::string path = entry.path().string();
        if (IsSystemVerilogFile(path)) {
          sv_files.push_back(path);
        }
      }
    }
  } catch (const std::exception& e) {
    Logger()->error("Error scanning directory {}: {}", directory, e.what());
  }

  co_return sv_files;
}

auto WorkspaceManager::IndexFiles(std::vector<std::string> file_paths)
    -> asio::awaitable<void> {
  Logger()->debug("WorkspaceManager processing {} files", file_paths.size());

  // Add all files to source loader
  for (const auto& path : file_paths) {
    source_loader_->addFiles(path);
  }

  // Create a bag with default options
  slang::Bag options;

  // Load and parse all sources
  std::vector<std::shared_ptr<slang::syntax::SyntaxTree>> syntax_trees;
  {
    ScopedTimer timer("Loading and parsing sources", Logger());
    syntax_trees = source_loader_->loadAndParseSources(options);
  }

  // Store the syntax trees in our map
  syntax_trees_.clear();

  // Create a mapping from file paths to their corresponding syntax trees
  // For simplicity, we'll use the file paths passed to the source loader as
  // keys and match them with syntax trees by their index
  for (size_t i = 0; i < file_paths.size() && i < syntax_trees.size(); i++) {
    if (syntax_trees[i]) {
      syntax_trees_[file_paths[i]] = syntax_trees[i];
    }
  }

  // Create and populate compilation
  {
    ScopedTimer timer("Creating compilation", Logger());
    // Create a new compilation with default options
    compilation_ = std::make_shared<slang::ast::Compilation>();

    // Add all syntax trees to the compilation
    for (auto& tree : syntax_trees) {
      if (tree) {
        compilation_->addSyntaxTree(tree);
      }
    }
  }

  // Check for source loader errors
  auto errors = source_loader_->getErrors();
  if (!errors.empty()) {
    Logger()->warn("Source loader encountered {} errors", errors.size());
    for (const auto& error : errors) {
      Logger()->warn("Source loader error: {}", error);
    }
  }

  Logger()->debug("Successfully processed {} files", syntax_trees_.size());
  co_return;
}

// Parse a single file and return its syntax tree
auto WorkspaceManager::ParseFile(std::string path)
    -> asio::awaitable<std::shared_ptr<slang::syntax::SyntaxTree>> {
  std::shared_ptr<slang::syntax::SyntaxTree> tree;

  // Create parsing options
  slang::Bag options;

  // Use fromFile which returns a TreeOrError type
  auto result =
      slang::syntax::SyntaxTree::fromFile(path, *source_manager_, options);

  if (!result) {
    // Handle error case - result contains error information
    auto [error_code, message] = result.error();
    Logger()->error(
        "WorkspaceManager failed to parse file {}: {}", path, message);
    co_return nullptr;
  }

  // Success case - extract the tree from the result
  tree = result.value();

  co_return tree;
}

// Handle a created file
auto WorkspaceManager::HandleFileCreated(std::string path)
    -> asio::awaitable<void> {
  Logger()->debug("WorkspaceManager handling created file: {}", path);

  // Check if the file is a SystemVerilog file
  if (!IsSystemVerilogFile(path)) {
    Logger()->debug(
        "WorkspaceManager skipping non-SystemVerilog file: {}", path);
    co_return;
  }

  // Check if the file exists and is readable
  if (!std::filesystem::exists(path)) {
    Logger()->warn("WorkspaceManager created file does not exist: {}", path);
    co_return;
  }

  // Parse the file
  auto tree = co_await ParseFile(path);
  if (!tree) {
    co_return;
  }

  // Add the syntax tree to our map
  syntax_trees_[path] = tree;

  Logger()->debug(
      "WorkspaceManager successfully added created file to workspace: {}",
      path);
  co_return;
}

// Handle a changed file
auto WorkspaceManager::HandleFileChanged(std::string path)
    -> asio::awaitable<void> {
  Logger()->debug("WorkspaceManager handling changed file: {}", path);

  // Check if this file is being tracked (if not, it might not be a
  // SystemVerilog file)
  if (syntax_trees_.find(path) == syntax_trees_.end()) {
    Logger()->debug(
        "WorkspaceManager changed file is not tracked in workspace: {}", path);
    co_return;
  }

  // Check if the file still exists
  if (!std::filesystem::exists(path)) {
    Logger()->warn("WorkspaceManager changed file no longer exists: {}", path);
    co_return;
  }

  // Re-parse the file
  auto tree = co_await ParseFile(path);
  if (!tree) {
    co_return;
  }

  // Update the syntax tree in our map
  syntax_trees_[path] = tree;

  Logger()->debug(
      "WorkspaceManager successfully updated changed file in workspace: {}",
      path);
  co_return;
}

// Handle a deleted file
auto WorkspaceManager::HandleFileDeleted(std::string path)
    -> asio::awaitable<void> {
  Logger()->debug("WorkspaceManager handling deleted file: {}", path);

  // Check if this file is being tracked in our workspace
  auto it = syntax_trees_.find(path);
  if (it == syntax_trees_.end()) {
    Logger()->debug(
        "WorkspaceManager deleted file was not tracked in workspace: {}", path);
    co_return;
  }

  // Remove the file from our syntax trees map
  syntax_trees_.erase(it);

  Logger()->debug(
      "WorkspaceManager successfully removed deleted file from workspace: {}",
      path);
  co_return;
}

// Rebuild compilation after file changes
auto WorkspaceManager::RebuildWorkspaceCompilation() -> asio::awaitable<void> {
  Logger()->debug(
      "WorkspaceManager rebuilding compilation with {} syntax trees",
      syntax_trees_.size());

  // Create a new compilation
  auto new_compilation = std::make_shared<slang::ast::Compilation>();

  // Use a timer to measure compilation time
  {
    ScopedTimer timer("Rebuilding compilation", Logger());

    // Add all syntax trees to the new compilation
    for (const auto& [path, tree] : syntax_trees_) {
      if (tree) {
        new_compilation->addSyntaxTree(tree);
      }
    }
  }

  // Replace the old compilation with the new one
  compilation_ = new_compilation;

  Logger()->debug("WorkspaceManager compilation rebuilt successfully");
  co_return;
}

// Dump workspace stats
void WorkspaceManager::DumpWorkspaceStats() {
  Logger()->info("WorkspaceManager Statistics:");
  Logger()->info("  Workspace folders: {}", workspace_folders_.size());
  Logger()->info("  Syntax trees: {}", syntax_trees_.size());
  Logger()->info("  Compilation active: {}", compilation_ != nullptr);

  // Find files that failed to parse
  size_t failed_files = 0;
  std::vector<std::string> sample_failed;

  for (const auto& folder : workspace_folders_) {
    std::vector<std::string> sv_files;
    // Find SV files in this folder
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(folder)) {
      if (entry.is_regular_file() &&
          IsSystemVerilogFile(entry.path().string())) {
        std::string path = entry.path().string();
        if (syntax_trees_.find(path) == syntax_trees_.end()) {
          failed_files++;
          if (sample_failed.size() < 5) {
            sample_failed.push_back(path);
          }
        }
      }
    }
  }

  Logger()->info("  Failed to parse: {}", failed_files);
  if (!sample_failed.empty()) {
    Logger()->info("  Sample failed files:");
    for (const auto& path : sample_failed) {
      Logger()->info("    - {}", path);
    }
  }
}

}  // namespace slangd

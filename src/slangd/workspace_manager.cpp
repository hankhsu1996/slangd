#include "slangd/workspace_manager.hpp"

#include <filesystem>

#include <slang/parsing/Lexer.h>
#include <slang/util/Bag.h>
#include <spdlog/spdlog.h>

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

const std::vector<std::string>& WorkspaceManager::GetWorkspaceFolders() const {
  return workspace_folders_;
}

auto WorkspaceManager::ScanWorkspace() -> asio::awaitable<void> {
  Logger()->debug(
      "WorkspaceManager starting workspace scan for SystemVerilog files");

  std::vector<std::string> all_files;

  for (const auto& folder : workspace_folders_) {
    Logger()->debug("WorkspaceManager scanning workspace folder: {}", folder);
    auto files = co_await FindSystemVerilogFiles(folder);
    Logger()->debug(
        "WorkspaceManager found {} SystemVerilog files in {}", files.size(),
        folder);

    // Collect all files
    all_files.insert(all_files.end(), files.begin(), files.end());
  }

  // Process all collected files together
  co_await ProcessFiles(all_files);

  Logger()->debug(
      "WorkspaceManager workspace scan completed. Total indexed files: {}",
      GetIndexedFileCount());

  co_return;
}

auto WorkspaceManager::FindSystemVerilogFiles(std::string directory)
    -> asio::awaitable<std::vector<std::string>> {
  std::vector<std::string> sv_files;

  Logger()->debug("WorkspaceManager scanning directory: {}", directory);

  try {
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(directory)) {
      if (entry.is_regular_file()) {
        std::string ext = entry.path().extension().string();
        if (ext == ".sv" || ext == ".svh" || ext == ".v" || ext == ".vh") {
          sv_files.push_back(entry.path().string());
        }
      }
    }
  } catch (const std::exception& e) {
    Logger()->error("Error scanning directory {}: {}", directory, e.what());
  }

  co_return sv_files;
}

auto WorkspaceManager::ProcessFiles(std::vector<std::string> file_paths)
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

auto WorkspaceManager::GetIndexedFileCount() const -> size_t {
  return syntax_trees_.size();
}

}  // namespace slangd

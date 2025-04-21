#include "slangd/core/workspace_manager.hpp"

#include <filesystem>
#include <utility>

#include <slang/diagnostics/DiagnosticEngine.h>
#include <slang/parsing/Lexer.h>
#include <slang/parsing/Preprocessor.h>
#include <slang/util/Bag.h>
#include <slangd/core/config_manager.hpp>
#include <spdlog/spdlog.h>

#include "slangd/utils/path_utils.hpp"
#include "slangd/utils/timer.hpp"

namespace slangd {

WorkspaceManager::WorkspaceManager(
    asio::any_io_executor executor, std::string workspace_folder,
    std::shared_ptr<ConfigManager> config_manager,
    std::shared_ptr<spdlog::logger> logger)
    : logger_(logger ? logger : spdlog::default_logger()),
      workspace_folder_(std::move(workspace_folder)),
      source_manager_(std::make_shared<slang::SourceManager>()),
      config_manager_(std::move(config_manager)),
      executor_(executor),
      strand_(asio::make_strand(executor)) {
}

// Factory method for test environments
std::shared_ptr<WorkspaceManager> WorkspaceManager::CreateForTesting(
    asio::any_io_executor executor,
    std::map<std::string, std::string> source_map,
    std::shared_ptr<spdlog::logger> logger) {
  auto workspace_manager = std::make_shared<WorkspaceManager>(
      executor, "/virtual/workspace",
      std::make_shared<ConfigManager>(executor, "/virtual/workspace"), logger);

  auto source_manager = workspace_manager->GetSourceManager();
  auto compilation = std::make_shared<slang::ast::Compilation>();

  // Create buffers, syntax trees and register them
  for (const auto& [uri, content] : source_map) {
    auto buffer = source_manager->assignText(uri, content);
    auto tree = slang::syntax::SyntaxTree::fromBuffer(buffer, *source_manager);
    compilation->addSyntaxTree(tree);

    // Register this buffer and tree
    workspace_manager->RegisterBuffer(uri, buffer.id, tree);
  }

  // Set the compilation
  workspace_manager->SetCompilation(compilation);

  return workspace_manager;
}

// Register a buffer and its syntax tree
void WorkspaceManager::RegisterBuffer(
    std::string uri, slang::BufferID buffer_id,
    std::shared_ptr<slang::syntax::SyntaxTree> syntax_tree) {
  std::string normalized_path = UriToNormalizedPath(uri);

  buffers_[normalized_path] = buffer_id;
  syntax_trees_[normalized_path] = syntax_tree;
}

// Add a file to the set of open files
void WorkspaceManager::AddOpenFile(std::string uri) {
  std::string normalized_path = UriToNormalizedPath(uri);

  auto it = buffers_.find(normalized_path);
  if (it != buffers_.end()) {
    slang::BufferID buffer_id = it->second;
    bool was_added = open_buffers_.insert(buffer_id).second;

    if (was_added) {
      RebuildSymbolIndex();
    }
  } else {
    logger_->warn("Attempted to open file without registered buffer: {}", uri);
  }
}

// Validate internal state consistency
auto WorkspaceManager::ValidateState() const -> bool {
  bool valid = true;

  // Check that all syntax trees have corresponding buffers
  for (const auto& [path, tree] : syntax_trees_) {
    if (buffers_.find(path) == buffers_.end()) {
      logger_->error(
          "Inconsistent state: syntax tree for path {} has no buffer", path);
      valid = false;
    }
  }

  // Check that all buffers have corresponding syntax trees
  for (const auto& [path, buffer_id] : buffers_) {
    if (syntax_trees_.find(path) == syntax_trees_.end()) {
      logger_->error(
          "Inconsistent state: buffer for path {} has no syntax tree", path);
      valid = false;
    }
  }

  // Check that all open buffers are registered in the buffers_ map
  for (const auto& buffer_id : open_buffers_) {
    bool found = false;
    for (const auto& [path, id] : buffers_) {
      if (id == buffer_id) {
        found = true;
        break;
      }
    }

    if (!found) {
      logger_->error(
          "Inconsistent state: open buffer ID {} not found in buffers map",
          buffer_id.getId());
      valid = false;
    }
  }

  return valid;
}

auto WorkspaceManager::ScanWorkspace() -> asio::awaitable<void> {
  logger_->debug(
      "WorkspaceManager starting workspace scan for SystemVerilog files");

  // Get source files based on config or auto-discovery
  auto all_files = config_manager_->GetSourceFiles();

  // Process all collected files together
  LoadAndCompileFiles(all_files);

  // Ensure the workspace symbol index is built
  if (!symbol_index_) {
    logger_->debug("WorkspaceManager building initial workspace symbol index");
    ScopedTimer timer("Building initial workspace symbol index", logger_);

    // Build the workspace symbol index
    symbol_index_ = std::make_shared<semantic::SymbolIndex>(
        semantic::SymbolIndex::FromCompilation(
            *compilation_, open_buffers_, logger_));

    logger_->debug("WorkspaceManager initial workspace symbol index built");
  }

  logger_->debug(
      "WorkspaceManager workspace scan completed. Total indexed files: {}",
      syntax_trees_.size());

  co_return;
}

auto WorkspaceManager::HandleFileChanges(std::vector<lsp::FileEvent> changes)
    -> asio::awaitable<void> {
  logger_->debug("WorkspaceManager handling {} file changes", changes.size());

  // Process each file change event
  for (const auto& change : changes) {
    // Convert URI to local file path
    if (!IsFileUri(change.uri)) {
      logger_->debug("WorkspaceManager skipping non-file URI: {}", change.uri);
      continue;
    }

    std::string local_path = UriToPath(change.uri);

    // Normalize path early to ensure consistent path handling
    std::string normalized_path = NormalizePath(local_path);

    // Check if this is a config file change - handled by ConfigManager already
    // We skip config files in WorkspaceManager since they're handled at the
    // server level
    if (IsConfigFile(normalized_path)) {
      logger_->debug(
          "WorkspaceManager skipping config file: {}", normalized_path);
      continue;
    }

    // Call the appropriate handler based on change type
    switch (change.type) {
      case lsp::FileChangeType::kCreated:
        logger_->debug("WorkspaceManager file created: {}", local_path);
        co_await HandleFileCreated(normalized_path);
        break;

      case lsp::FileChangeType::kChanged:
        logger_->debug("WorkspaceManager file changed: {}", local_path);
        co_await HandleFileChanged(normalized_path);
        break;

      case lsp::FileChangeType::kDeleted:
        logger_->debug("WorkspaceManager file deleted: {}", local_path);
        co_await HandleFileDeleted(normalized_path);
        break;
    }
  }

  // Rebuild compilation after processing all changes
  co_await RebuildWorkspaceCompilation();

  co_return;
}

void WorkspaceManager::RebuildSymbolIndex() {
  if (compilation_) {
    symbol_index_ = std::make_shared<semantic::SymbolIndex>(
        semantic::SymbolIndex::FromCompilation(
            *compilation_, open_buffers_, logger_));
  }
}

void WorkspaceManager::LoadAndCompileFiles(
    std::vector<std::string> file_paths) {
  logger_->debug("WorkspaceManager processing {} files", file_paths.size());

  // First, normalize all paths for consistent handling
  std::vector<std::string> normalized_paths;
  normalized_paths.reserve(file_paths.size());

  for (const auto& path : file_paths) {
    normalized_paths.push_back(NormalizePath(path));
  }

  // Clear the maps before adding new files
  syntax_trees_.clear();
  buffers_.clear();

  // Create a new compilation
  auto new_compilation = std::make_shared<slang::ast::Compilation>();

  // For each path, we'll parse the file and add it to the compilation
  for (const auto& path : normalized_paths) {
    auto [buffer_id, syntax_tree] = ParseFile(path);
    if (syntax_tree) {
      // Use URI for consistent approach across real and virtual files
      std::string uri = PathToUri(path);

      // Register this buffer and tree
      RegisterBuffer(uri, buffer_id, syntax_tree);

      // Add to compilation
      new_compilation->addSyntaxTree(syntax_tree);

      logger_->debug("Added file to compilation: {}", path);
    }
  }

  // Set the new compilation
  compilation_ = new_compilation;

  logger_->debug("Successfully processed {} files", syntax_trees_.size());
}

// Parse a single file and return its syntax tree
auto WorkspaceManager::ParseFile(std::string path)
    -> std::pair<slang::BufferID, std::shared_ptr<slang::syntax::SyntaxTree>> {
  // Create parsing options with default settings
  slang::Bag options;

  // Use fromFile which returns a TreeOrError type
  auto buffer_or_error = source_manager_->readSource(path, nullptr);
  if (!buffer_or_error) {
    logger_->error("WorkspaceManager failed to read file {}", path);
    return std::make_pair(slang::BufferID(), nullptr);
  }
  auto buffer = buffer_or_error.value();
  auto syntax_tree =
      slang::syntax::SyntaxTree::fromBuffer(buffer, *source_manager_, options);

  return std::make_pair(buffer.id, syntax_tree);
}

// Handle a created file
auto WorkspaceManager::HandleFileCreated(std::string path)
    -> asio::awaitable<void> {
  logger_->debug("WorkspaceManager handling created file: {}", path);

  // Check if the file is a SystemVerilog file
  if (!IsSystemVerilogFile(path)) {
    logger_->debug(
        "WorkspaceManager skipping non-SystemVerilog file: {}", path);
    co_return;
  }

  // If using config, check if this file is part of the config
  if (config_manager_ && config_manager_->HasValidConfig()) {
    bool in_config = false;
    auto all_files = config_manager_->GetSourceFiles();
    for (const auto& config_file : all_files) {
      if (NormalizePath(config_file) == NormalizePath(path)) {
        in_config = true;
        break;
      }
    }

    if (!in_config) {
      logger_->debug(
          "WorkspaceManager skipping file not in configuration: {}", path);
      co_return;
    }
  }

  // Check if the file exists and is readable
  if (!std::filesystem::exists(path)) {
    logger_->warn("WorkspaceManager created file does not exist: {}", path);
    co_return;
  }

  // Parse the file
  auto [buffer_id, syntax_tree] = ParseFile(path);
  if (!syntax_tree) {
    co_return;
  }

  // Add the syntax tree to our map
  syntax_trees_[path] = syntax_tree;
  co_return;
}

// Handle a changed file
auto WorkspaceManager::HandleFileChanged(std::string path)
    -> asio::awaitable<void> {
  logger_->debug("WorkspaceManager handling changed file: {}", path);

  // Check if this file is being tracked (if not, it might not be a
  // SystemVerilog file or not in the current config)
  if (syntax_trees_.find(path) == syntax_trees_.end()) {
    logger_->debug(
        "WorkspaceManager changed file is not tracked in workspace: {}", path);
    co_return;
  }

  // Check if the file still exists
  if (!std::filesystem::exists(path)) {
    logger_->warn("WorkspaceManager changed file no longer exists: {}", path);
    co_return;
  }

  // Re-parse the file
  auto [buffer_id, syntax_tree] = ParseFile(path);
  if (!syntax_tree) {
    co_return;
  }

  // Update the syntax tree in our map
  syntax_trees_[path] = syntax_tree;
  co_return;
}

// Handle a deleted file
auto WorkspaceManager::HandleFileDeleted(std::string path)
    -> asio::awaitable<void> {
  logger_->debug("WorkspaceManager handling deleted file: {}", path);

  // Check if this file is being tracked in our workspace
  auto it = syntax_trees_.find(path);
  if (it == syntax_trees_.end()) {
    logger_->debug(
        "WorkspaceManager deleted file was not tracked in workspace: {}", path);
    co_return;
  }

  // Remove the file from our syntax trees map
  syntax_trees_.erase(it);

  logger_->debug(
      "WorkspaceManager successfully removed deleted file from workspace: {}",
      path);
  co_return;
}

// Rebuild compilation after file changes
auto WorkspaceManager::RebuildWorkspaceCompilation() -> asio::awaitable<void> {
  logger_->debug(
      "WorkspaceManager rebuilding compilation with {} syntax trees",
      syntax_trees_.size());

  // Create a new compilation
  auto new_compilation = std::make_shared<slang::ast::Compilation>();

  // Use a timer to measure compilation time
  {
    ScopedTimer timer("Rebuilding compilation", logger_);

    // Add all syntax trees to the new compilation
    for (const auto& [path, tree] : syntax_trees_) {
      if (tree) {
        new_compilation->addSyntaxTree(tree);
      }
    }
  }

  // Replace the old compilation with the new one
  compilation_ = new_compilation;

  // Build a workspace symbol index
  RebuildSymbolIndex();

  // Validate state to ensure consistency
  if (!ValidateState()) {
    logger_->warn(
        "Workspace state validation failed after rebuilding compilation");
  }

  logger_->debug("WorkspaceManager compilation rebuilt successfully");
  co_return;
}

// Dump workspace stats
auto WorkspaceManager::DumpWorkspaceStats() -> void {
  logger_->info("WorkspaceManager Statistics:");
  logger_->info("  Workspace folder: {}", workspace_folder_);
  logger_->info("  Syntax trees: {}", syntax_trees_.size());
  logger_->info("  Compilation active: {}", compilation_ != nullptr);

  // Find files that failed to parse
  std::vector<std::string> failed_files;

  try {
    // Find SV files in this folder
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(workspace_folder_)) {
      if (entry.is_regular_file() &&
          IsSystemVerilogFile(entry.path().string())) {
        std::string path = entry.path().string();
        // Normalize the path to match how we store paths in syntax_trees_
        std::string normalized_path = NormalizePath(path);

        if (syntax_trees_.find(normalized_path) == syntax_trees_.end()) {
          failed_files.push_back(path);  // Keep original path for display
        }
      }
    }
  } catch (const std::exception& e) {
    logger_->warn(
        "Error scanning directory {}: {}", workspace_folder_, e.what());
  }

  logger_->info("  Failed to parse: {}", failed_files.size());

  if (!failed_files.empty()) {
    logger_->info("  Sample failed files:");

    // Take at most 5 samples to avoid too much output
    size_t samples = std::min(failed_files.size(), static_cast<size_t>(5));
    for (size_t i = 0; i < samples; i++) {
      logger_->info("    - {}", failed_files[i]);
    }

    // Try to parse one of the failed files with detailed diagnostics
    if (!failed_files.empty()) {
      std::string test_file = failed_files[0];
      logger_->info("  Diagnostic parse attempt for: {}", test_file);

      // Create parsing options with diagnostic collection
      slang::Bag options;

      // Try parsing with detailed diagnostics
      auto result = slang::syntax::SyntaxTree::fromFile(
          test_file, *source_manager_, options);

      if (!result) {
        // Handle error case - result contains error information
        auto [error_code, message] = result.error();
        logger_->info(
            "  Parse error: {} (code: {})", message, error_code.value());
      } else {
        // We got a tree but it might have diagnostics
        auto tree = result.value();
        const auto& diagnostics = tree->diagnostics();

        if (diagnostics.empty()) {
          logger_->info(
              "  Surprisingly, diagnostic parse succeeded with no errors");

          // Special check: see if the file is actually in our syntax tree map
          // under a different path
          std::string canonical_path = NormalizePath(test_file);
          if (syntax_trees_.find(canonical_path) != syntax_trees_.end()) {
            logger_->info(
                "  But file IS tracked under normalized path: {}",
                canonical_path);
          }
        } else {
          logger_->info("  Parse diagnostics ({}): ", diagnostics.size());

          // Create a diagnostic engine to format messages
          slang::DiagnosticEngine diag_engine(*source_manager_);

          for (size_t i = 0;
               i < std::min(diagnostics.size(), static_cast<size_t>(5)); i++) {
            const auto& diag = diagnostics[i];
            logger_->info(
                "    - {}: {}", slang::toString(diag.code),
                diag_engine.formatMessage(diag));
          }
        }
      }
    }
  }
}

}  // namespace slangd

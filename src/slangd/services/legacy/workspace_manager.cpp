#include "slangd/services/legacy/workspace_manager.hpp"

#include <filesystem>
#include <utility>

#include <slang/diagnostics/DiagnosticEngine.h>
#include <slang/parsing/Lexer.h>
#include <slang/parsing/Preprocessor.h>
#include <slang/util/Bag.h>
#include <slangd/core/project_layout_service.hpp>
#include <spdlog/spdlog.h>

#include "slangd/utils/path_utils.hpp"
#include "slangd/utils/timer.hpp"

namespace slangd {

WorkspaceManager::WorkspaceManager(
    asio::any_io_executor executor, CanonicalPath workspace_folder,
    std::shared_ptr<ProjectLayoutService> layout_service,
    std::shared_ptr<spdlog::logger> logger)
    : logger_(logger ? logger : spdlog::default_logger()),
      workspace_folder_(std::move(workspace_folder)),
      source_manager_(std::make_shared<slang::SourceManager>()),
      layout_service_(std::move(layout_service)),
      executor_(executor),
      strand_(asio::make_strand(executor)) {
}

// Factory method for test environments
auto WorkspaceManager::CreateForTesting(
    asio::any_io_executor executor,
    std::map<std::string, std::string> source_map,
    std::shared_ptr<spdlog::logger> logger)
    -> std::shared_ptr<WorkspaceManager> {
  auto workspace_root = CanonicalPath::CurrentPath();

  // Create ProjectLayoutBuilder dependencies
  auto layout_service =
      ProjectLayoutService::Create(executor, workspace_root, logger);
  auto workspace_manager = std::make_shared<WorkspaceManager>(
      executor, workspace_root, layout_service, logger);

  auto source_manager = workspace_manager->GetSourceManager();
  auto compilation = std::make_shared<slang::ast::Compilation>();

  for (const auto& [uri, content] : source_map) {
    auto path = CanonicalPath::FromUri(uri);
    auto buffer = source_manager->assignText(path.String(), content);
    auto tree = slang::syntax::SyntaxTree::fromBuffer(buffer, *source_manager);

    workspace_manager->RegisterBuffer(path, buffer.id, tree);
    compilation->addSyntaxTree(tree);
  }

  workspace_manager->SetCompilation(compilation);
  return workspace_manager;
}

// Register a buffer and its syntax tree
void WorkspaceManager::RegisterBuffer(
    CanonicalPath path, slang::BufferID buffer_id,
    std::shared_ptr<slang::syntax::SyntaxTree> syntax_tree) {
  buffer_ids_[path] = buffer_id;
  syntax_trees_[path] = syntax_tree;
}

// Add a file to the set of open files
auto WorkspaceManager::AddOpenFile(CanonicalPath path)
    -> asio::awaitable<void> {
  logger_->debug("WorkspaceManager adding open file: {}", path);
  co_await asio::post(strand_, asio::use_awaitable);

  auto it = buffer_ids_.find(path);
  if (it != buffer_ids_.end()) {
    slang::BufferID buffer_id = it->second;
    open_buffers_.insert(buffer_id);
    RebuildSymbolIndex();
  } else {
    logger_->warn(
        "WorkspaceManager attempted to open file without registered buffer: {}",
        path);
  }
}

// Check layout version and rebuild workspace if changed
auto WorkspaceManager::MaybeRebuildIfLayoutChanged() -> asio::awaitable<void> {
  co_await asio::post(strand_, asio::use_awaitable);

  auto snapshot = layout_service_->GetLayoutSnapshot();
  if (snapshot.version > cached_layout_version_) {
    logger_->debug(
        "WorkspaceManager: Layout version changed from {} to {}, rebuilding "
        "workspace",
        cached_layout_version_, snapshot.version);

    cached_layout_version_ = snapshot.version;
    cached_layout_ = snapshot.layout;

    // Trigger workspace rebuild with new layout
    LoadAndCompileFiles(cached_layout_->GetFiles());

    // Rebuild symbol index after new compilation
    RebuildSymbolIndex();
  }
}

// Validate internal state consistency
auto WorkspaceManager::ValidateState() const -> bool {
  bool valid = true;

  // Check that all syntax trees have corresponding buffers
  for (const auto& [path, tree] : syntax_trees_) {
    if (buffer_ids_.find(path) == buffer_ids_.end()) {
      logger_->error(
          "WorkspaceManager inconsistent state: syntax tree for path {} has no "
          "buffer",
          path);
      valid = false;
    }
  }

  // Check that all buffers have corresponding syntax trees
  for (const auto& [path, buffer_id] : buffer_ids_) {
    if (syntax_trees_.find(path) == syntax_trees_.end()) {
      logger_->error(
          "WorkspaceManager inconsistent state: buffer for path {} has no "
          "syntax tree",
          path);
      valid = false;
    }
  }

  // Check that all open buffers are registered in the buffers_ map
  for (const auto& buffer_id : open_buffers_) {
    bool found = false;
    for (const auto& [path, id] : buffer_ids_) {
      if (id == buffer_id) {
        found = true;
        break;
      }
    }

    if (!found) {
      logger_->error(
          "WorkspaceManager inconsistent state: open buffer ID {} not found in "
          "buffers map",
          buffer_id.getId());
      valid = false;
    }
  }

  return valid;
}

auto WorkspaceManager::ScanWorkspace() -> asio::awaitable<void> {
  logger_->debug(
      "WorkspaceManager starting workspace scan for SystemVerilog files");
  co_await asio::post(strand_, asio::use_awaitable);

  // Check if layout has changed and rebuild if necessary
  co_await MaybeRebuildIfLayoutChanged();

  // Ensure the workspace symbol index is built
  if (!symbol_index_) {
    RebuildSymbolIndex();
  }

  logger_->debug(
      "WorkspaceManager workspace scan completed. Total indexed files: {}",
      syntax_trees_.size());

  co_return;
}

auto WorkspaceManager::HandleFileChanges(std::vector<lsp::FileEvent> changes)
    -> asio::awaitable<void> {
  logger_->debug("WorkspaceManager handling {} file changes", changes.size());
  co_await asio::post(strand_, asio::use_awaitable);

  // Process each file change event
  for (const auto& change : changes) {
    CanonicalPath path = CanonicalPath::FromUri(change.uri);

    // Check if this is a config file change - handled by ConfigManager already
    // We skip config files in WorkspaceManager since they're handled at the
    // server level
    if (IsConfigFile(path.Path())) {
      logger_->debug("WorkspaceManager skipping config file: {}", path);
      continue;
    }

    // Call the appropriate handler based on change type
    switch (change.type) {
      case lsp::FileChangeType::kCreated:
        logger_->debug("WorkspaceManager file created: {}", path);
        co_await HandleFileCreated(path);
        break;

      case lsp::FileChangeType::kChanged:
        logger_->debug("WorkspaceManager file changed: {}", path);
        co_await HandleFileChanged(path);
        break;

      case lsp::FileChangeType::kDeleted:
        logger_->debug("WorkspaceManager file deleted: {}", path);
        co_await HandleFileDeleted(path);
        break;
    }
  }

  // Rebuild compilation after processing all changes
  co_await RebuildWorkspaceCompilation();

  co_return;
}

void WorkspaceManager::RebuildSymbolIndex() {
  logger_->debug("WorkspaceManager rebuilding symbol index");
  if (compilation_) {
    symbol_index_ = std::make_shared<semantic::SymbolIndex>(
        semantic::SymbolIndex::FromCompilation(
            *compilation_, open_buffers_, logger_));
  }
}

void WorkspaceManager::LoadAndCompileFiles(
    std::vector<CanonicalPath> file_paths) {
  logger_->debug("WorkspaceManager processing {} files", file_paths.size());

  // First, normalize all paths for consistent handling
  std::vector<CanonicalPath> normalized_paths;
  normalized_paths.reserve(file_paths.size());

  for (const auto& path : file_paths) {
    normalized_paths.push_back(path);
  }

  // Clear the maps before adding new files
  syntax_trees_.clear();
  buffer_ids_.clear();

  // Create a new compilation
  auto new_compilation = std::make_shared<slang::ast::Compilation>();

  // For each path, we'll parse the file and add it to the compilation
  for (const auto& path : normalized_paths) {
    auto [buffer_id, syntax_tree] = ParseFile(path);
    if (syntax_tree) {
      // Register this buffer and tree
      RegisterBuffer(path, buffer_id, syntax_tree);

      // Add to compilation
      new_compilation->addSyntaxTree(syntax_tree);
    }
  }

  // Set the new compilation
  compilation_ = new_compilation;

  logger_->debug("Successfully processed {} files", syntax_trees_.size());
}

// Parse a single file and return its syntax tree
auto WorkspaceManager::ParseFile(CanonicalPath path)
    -> std::pair<slang::BufferID, std::shared_ptr<slang::syntax::SyntaxTree>> {
  // Create parsing options with default settings
  slang::Bag options;

  // Use fromFile which returns a TreeOrError type
  auto buffer_or_error = source_manager_->readSource(path.Path(), nullptr);
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
auto WorkspaceManager::HandleFileCreated(CanonicalPath path)
    -> asio::awaitable<void> {
  logger_->debug("WorkspaceManager handling created file: {}", path);

  // Check if the file is a SystemVerilog file
  if (!IsSystemVerilogFile(path.Path())) {
    logger_->debug(
        "WorkspaceManager skipping non-SystemVerilog file: {}", path);
    co_return;
  }

  // If using config, check if this file is part of the cached layout
  if (layout_service_ && layout_service_->HasValidConfig()) {
    // Ensure we have the latest layout
    if (!cached_layout_) {
      auto snapshot = layout_service_->GetLayoutSnapshot();
      cached_layout_version_ = snapshot.version;
      cached_layout_ = snapshot.layout;
    }

    bool in_config = false;
    const auto& all_files = cached_layout_->GetFiles();
    for (const auto& config_file : all_files) {
      if (config_file == path) {
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
  if (!std::filesystem::exists(path.Path())) {
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
auto WorkspaceManager::HandleFileChanged(CanonicalPath path)
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
  if (!std::filesystem::exists(path.Path())) {
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
auto WorkspaceManager::HandleFileDeleted(CanonicalPath path)
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
  std::vector<CanonicalPath> failed_files;

  try {
    // Find SV files in this folder
    for (const auto& entry : std::filesystem::recursive_directory_iterator(
             workspace_folder_.Path())) {
      if (entry.is_regular_file() && IsSystemVerilogFile(entry.path())) {
        // Normalize the path to match how we store paths in syntax_trees_
        CanonicalPath path = CanonicalPath(entry.path());

        if (syntax_trees_.find(path) == syntax_trees_.end()) {
          failed_files.push_back(path);
        }
      }
    }
  } catch (const std::exception& e) {
    logger_->warn(
        "Error scanning directory {}: {}", workspace_folder_.String(),
        e.what());
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
      CanonicalPath test_file = failed_files[0];
      logger_->info("  Diagnostic parse attempt for: {}", test_file);

      // Create parsing options with diagnostic collection
      slang::Bag options;

      // Try parsing with detailed diagnostics
      auto result = slang::syntax::SyntaxTree::fromFile(
          test_file.Path().string(), *source_manager_, options);

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
          auto canonical_path = CanonicalPath(test_file.Path());
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

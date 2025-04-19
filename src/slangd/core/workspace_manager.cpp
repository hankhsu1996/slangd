#include "slangd/core/workspace_manager.hpp"

#include <filesystem>
#include <fstream>
#include <utility>

#include <slang/diagnostics/DiagnosticEngine.h>
#include <slang/parsing/Lexer.h>
#include <slang/parsing/Preprocessor.h>
#include <slang/util/Bag.h>
#include <slangd/core/config_manager.hpp>
#include <spdlog/spdlog.h>

#include "slangd/utils/source_utils.hpp"
#include "slangd/utils/timer.hpp"
#include "slangd/utils/uri.hpp"

namespace slangd {

WorkspaceManager::WorkspaceManager(
    asio::any_io_executor executor, std::string workspace_folder,
    std::shared_ptr<ConfigManager> config_manager,
    std::shared_ptr<spdlog::logger> logger)
    : logger_(logger ? logger : spdlog::default_logger()),
      workspace_folder_(std::move(workspace_folder)),
      source_manager_(std::make_shared<slang::SourceManager>()),
      source_loader_(
          std::make_unique<slang::driver::SourceLoader>(*source_manager_)),
      config_manager_(config_manager),
      executor_(executor),
      strand_(asio::make_strand(executor)) {
}

auto WorkspaceManager::ScanWorkspace() -> asio::awaitable<void> {
  Logger()->debug(
      "WorkspaceManager starting workspace scan for SystemVerilog files");

  // Get source files based on config or auto-discovery
  auto all_files = co_await GetWorkspaceSourceFiles();

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

    // Normalize path early to ensure consistent path handling
    std::string normalized_path = NormalizePath(local_path);

    // Check if this is a config file change - handled by ConfigManager already
    // We skip config files in WorkspaceManager since they're handled at the
    // server level
    if (ConfigManager::IsConfigFile(normalized_path)) {
      Logger()->debug(
          "WorkspaceManager skipping config file: {}", normalized_path);
      continue;
    }

    // Call the appropriate handler based on change type
    switch (change.type) {
      case lsp::FileChangeType::kCreated:
        Logger()->debug("WorkspaceManager file created: {}", local_path);
        co_await HandleFileCreated(normalized_path);
        break;

      case lsp::FileChangeType::kChanged:
        Logger()->debug("WorkspaceManager file changed: {}", local_path);
        co_await HandleFileChanged(normalized_path);
        break;

      case lsp::FileChangeType::kDeleted:
        Logger()->debug("WorkspaceManager file deleted: {}", local_path);
        co_await HandleFileDeleted(normalized_path);
        break;
    }
  }

  // Rebuild compilation after processing all changes
  co_await RebuildWorkspaceCompilation();

  co_return;
}

auto WorkspaceManager::GetWorkspaceSourceFiles()
    -> asio::awaitable<std::vector<std::string>> {
  // Use files from config if available
  if (config_manager_ && config_manager_->HasValidConfig()) {
    // Get files from config
    const auto& config = config_manager_->GetConfig();

    // Combine the files directly specified in the config
    std::vector<std::string> all_files = config->GetFiles();

    // Process file lists if present
    const auto& file_lists = config->GetFileLists();
    for (const auto& file_list_path : file_lists.paths) {
      std::string resolved_path = file_list_path;
      if (!file_lists.absolute && !workspace_folder_.empty()) {
        resolved_path =
            (std::filesystem::path(workspace_folder_) / file_list_path)
                .string();
      }

      // Process the file list and add files
      auto files_from_list =
          ProcessFileList(resolved_path, file_lists.absolute);
      all_files.insert(
          all_files.end(), files_from_list.begin(), files_from_list.end());
    }

    Logger()->debug(
        "WorkspaceManager using {} source files from config file",
        all_files.size());
    co_return all_files;
  } else {
    // Fall back to auto-discovery
    std::vector<std::string> all_files;
    Logger()->debug(
        "WorkspaceManager scanning workspace folder: {}", workspace_folder_);
    auto files = co_await FindSystemVerilogFilesInDirectory(workspace_folder_);
    Logger()->debug(
        "WorkspaceManager found {} SystemVerilog files in {}", files.size(),
        workspace_folder_);
    all_files.insert(all_files.end(), files.begin(), files.end());
    co_return all_files;
  }
}

auto WorkspaceManager::ProcessFileList(const std::string& path, bool absolute)
    -> std::vector<std::string> {
  std::vector<std::string> files;

  // Read the filelist content
  std::ifstream file(path);
  if (!file) {
    Logger()->warn("WorkspaceManager failed to read filelist: {}", path);
    return files;
  }

  std::string line;
  std::string accumulated_line;
  while (std::getline(file, line)) {
    // Skip empty lines and comments
    // Remove leading/trailing whitespace
    while (!line.empty() && (std::isspace(line.front()) != 0)) {
      line.erase(0, 1);
    }
    while (!line.empty() && (std::isspace(line.back()) != 0)) {
      line.pop_back();
    }

    if (line.empty() || line[0] == '#' || line[0] == '/') {
      continue;
    }

    // Handle line continuation
    if (!line.empty() && line.back() == '\\') {
      accumulated_line += line.substr(0, line.size() - 1);
      continue;
    }

    // Process the complete line
    accumulated_line += line;
    if (!accumulated_line.empty()) {
      std::string file_path = accumulated_line;
      if (!absolute) {
        auto filelist_dir = std::filesystem::path(path).parent_path();
        file_path = (filelist_dir / file_path).string();
      }
      files.push_back(file_path);
    }
    accumulated_line.clear();
  }

  return files;
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
          // Normalize path before adding to ensure consistency
          sv_files.push_back(NormalizePath(path));
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

  // First, normalize all paths for consistent handling
  std::vector<std::string> normalized_paths;
  normalized_paths.reserve(file_paths.size());

  for (const auto& path : file_paths) {
    normalized_paths.push_back(NormalizePath(path));
  }

  // Add all files to source loader
  for (const auto& path : normalized_paths) {
    source_loader_->addFiles(path);
  }

  // Add include directories to source loader
  if (config_manager_->HasValidConfig()) {
    const auto& config = config_manager_->GetConfig();
    for (const auto& include_dir : config->GetIncludeDirs()) {
      source_loader_->addSearchDirectories(include_dir);
    }
    Logger()->debug(
        "WorkspaceManager added {} include directories",
        config->GetIncludeDirs().size());
  }
  // Add all the subdirectories of the workspace folders to the source loader
  else {
    int count = 0;
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(workspace_folder_)) {
      if (entry.is_directory()) {
        source_loader_->addSearchDirectories(entry.path().string());
        count++;
      }
    }
    Logger()->debug("WorkspaceManager added {} include directories", count);
  }

  // Add defines to source loader
  slang::parsing::PreprocessorOptions pp_options;
  if (config_manager_->HasValidConfig()) {
    const auto& config = config_manager_->GetConfig();
    for (const auto& define : config->GetDefines()) {
      Logger()->debug("WorkspaceManager adding define: {}", define);
      pp_options.predefines.push_back(define);
    }
  }

  // Create an options bag with the preprocessor options
  slang::Bag options;
  options.set(pp_options);

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
  for (size_t i = 0; i < normalized_paths.size() && i < syntax_trees.size();
       i++) {
    if (syntax_trees[i]) {
      syntax_trees_[normalized_paths[i]] = syntax_trees[i];
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
  // Create parsing options with default settings
  slang::Bag options;

  // Use fromFile which returns a TreeOrError type
  auto result =
      slang::syntax::SyntaxTree::fromFile(path, *source_manager_, options);

  if (!result) {
    // Handle critical error case - result contains error information
    auto [error_code, message] = result.error();
    Logger()->error(
        "WorkspaceManager failed to open file {}: {}", path, message);
    co_return nullptr;
  }

  co_return result.value();
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

  // If using config, check if this file is part of the config
  if (config_manager_ && config_manager_->HasValidConfig()) {
    bool in_config = false;
    auto all_files = co_await GetWorkspaceSourceFiles();
    for (const auto& config_file : all_files) {
      if (NormalizePath(config_file) == NormalizePath(path)) {
        in_config = true;
        break;
      }
    }

    if (!in_config) {
      Logger()->debug(
          "WorkspaceManager skipping file not in configuration: {}", path);
      co_return;
    }
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
  // SystemVerilog file or not in the current config)
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
auto WorkspaceManager::DumpWorkspaceStats() -> void {
  Logger()->info("WorkspaceManager Statistics:");
  Logger()->info("  Workspace folder: {}", workspace_folder_);
  Logger()->info("  Using config file: {}", config_manager_->HasValidConfig());
  Logger()->info("  Syntax trees: {}", syntax_trees_.size());
  Logger()->info("  Compilation active: {}", compilation_ != nullptr);

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
    Logger()->warn(
        "Error scanning directory {}: {}", workspace_folder_, e.what());
  }

  Logger()->info("  Failed to parse: {}", failed_files.size());

  if (!failed_files.empty()) {
    Logger()->info("  Sample failed files:");

    // Take at most 5 samples to avoid too much output
    size_t samples = std::min(failed_files.size(), static_cast<size_t>(5));
    for (size_t i = 0; i < samples; i++) {
      Logger()->info("    - {}", failed_files[i]);
    }

    // Try to parse one of the failed files with detailed diagnostics
    if (!failed_files.empty()) {
      std::string test_file = failed_files[0];
      Logger()->info("  Diagnostic parse attempt for: {}", test_file);

      // Create parsing options with diagnostic collection
      slang::Bag options;

      // Try parsing with detailed diagnostics
      auto result = slang::syntax::SyntaxTree::fromFile(
          test_file, *source_manager_, options);

      if (!result) {
        // Handle error case - result contains error information
        auto [error_code, message] = result.error();
        Logger()->info(
            "  Parse error: {} (code: {})", message, error_code.value());
      } else {
        // We got a tree but it might have diagnostics
        auto tree = result.value();
        const auto& diagnostics = tree->diagnostics();

        if (diagnostics.empty()) {
          Logger()->info(
              "  Surprisingly, diagnostic parse succeeded with no errors");

          // Special check: see if the file is actually in our syntax tree map
          // under a different path
          std::string canonical_path = NormalizePath(test_file);
          if (syntax_trees_.find(canonical_path) != syntax_trees_.end()) {
            Logger()->info(
                "  But file IS tracked under normalized path: {}",
                canonical_path);
          }
        } else {
          Logger()->info("  Parse diagnostics ({}): ", diagnostics.size());

          // Create a diagnostic engine to format messages
          slang::DiagnosticEngine diag_engine(*source_manager_);

          for (size_t i = 0;
               i < std::min(diagnostics.size(), static_cast<size_t>(5)); i++) {
            const auto& diag = diagnostics[i];
            Logger()->info(
                "    - {}: {}", slang::toString(diag.code),
                diag_engine.formatMessage(diag));
          }
        }
      }
    }
  }
}

}  // namespace slangd

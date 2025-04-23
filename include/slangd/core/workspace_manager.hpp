#pragma once

#include <map>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include <slang/ast/Compilation.h>
#include <slang/driver/Driver.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/text/SourceManager.h>
#include <spdlog/spdlog.h>

#include "lsp/workspace.hpp"
#include "slangd/core/config_manager.hpp"
#include "slangd/semantic/symbol_index.hpp"

namespace slangd {

class WorkspaceManager {
 public:
  WorkspaceManager(
      asio::any_io_executor executor, std::filesystem::path workspace_folder,
      std::shared_ptr<ConfigManager> config_manager,
      std::shared_ptr<spdlog::logger> logger = nullptr);

  // Factory method to create a WorkspaceManager for testing with in-memory
  // buffers
  static auto CreateForTesting(
      asio::any_io_executor executor,
      std::map<std::filesystem::path, std::string> source_map,
      std::shared_ptr<spdlog::logger> logger = nullptr)
      -> std::shared_ptr<WorkspaceManager>;

  // Scan the workspace to find and process all SystemVerilog files
  auto ScanWorkspace() -> asio::awaitable<void>;

  // Handle LSP file change events
  auto HandleFileChanges(std::vector<lsp::FileEvent> changes)
      -> asio::awaitable<void>;

  // Rebuild the symbol index
  void RebuildSymbolIndex();

  // Get the compilation for this workspace
  [[nodiscard]] auto GetCompilation() const
      -> std::shared_ptr<slang::ast::Compilation> {
    return compilation_;
  }

  // Set the compilation for this workspace
  void SetCompilation(std::shared_ptr<slang::ast::Compilation> compilation) {
    compilation_ = compilation;
  }

  // Get the source manager
  [[nodiscard]] auto GetSourceManager() const
      -> std::shared_ptr<slang::SourceManager> {
    return source_manager_;
  }

  // Get the buffer ID from a path
  [[nodiscard]] auto GetBufferIdFromPath(std::filesystem::path path) const
      -> slang::BufferID {
    auto it = buffers_.find(path);
    if (it == buffers_.end()) {
      logger_->error(
          "WorkspaceManager: No buffer ID found for path: {}", path.string());
      return slang::BufferID{};
    }
    return it->second;
  }

  // Get the workspace symbol index
  auto GetSymbolIndex() const -> std::shared_ptr<semantic::SymbolIndex> {
    return symbol_index_;
  }

  // Output debugging statistics for the workspace
  auto DumpWorkspaceStats() -> void;

  // Register a buffer and its syntax tree - explicitly manages internal state
  void RegisterBuffer(
      std::filesystem::path path, slang::BufferID buffer_id,
      std::shared_ptr<slang::syntax::SyntaxTree> syntax_tree);

  // Track open files for better indexing
  void AddOpenFile(std::filesystem::path path);

  // Check if the workspace has valid internal state
  auto ValidateState() const -> bool;

 private:
  // Process a list of source files to create syntax trees and compilation
  // This method change the internal state of the workspace manager
  void LoadAndCompileFiles(std::vector<std::filesystem::path> file_paths);

  // Parse a single file
  auto ParseFile(std::filesystem::path path)
      -> std::pair<slang::BufferID, std::shared_ptr<slang::syntax::SyntaxTree>>;

  // Event handlers for file changes
  auto HandleFileCreated(std::filesystem::path path) -> asio::awaitable<void>;
  auto HandleFileChanged(std::filesystem::path path) -> asio::awaitable<void>;
  auto HandleFileDeleted(std::filesystem::path path) -> asio::awaitable<void>;

  // Rebuild the workspace compilation after file changes
  auto RebuildWorkspaceCompilation() -> asio::awaitable<void>;

  // Logger instance
  std::shared_ptr<spdlog::logger> logger_;

  // Workspace folder path
  std::filesystem::path workspace_folder_;

  // Source manager for all workspace files
  std::shared_ptr<slang::SourceManager> source_manager_;

  // Map of file paths to their source buffers
  // The key is the normalized path
  std::map<std::filesystem::path, slang::BufferID> buffers_;

  // Map of file paths to their syntax trees
  // The key is the normalized path
  std::map<std::filesystem::path, std::shared_ptr<slang::syntax::SyntaxTree>>
      syntax_trees_;

  // The compilation for this workspace
  std::shared_ptr<slang::ast::Compilation> compilation_;

  // The configuration manager
  std::shared_ptr<ConfigManager> config_manager_;

  // Workspace symbol index
  std::shared_ptr<semantic::SymbolIndex> symbol_index_{nullptr};

  // Track open buffers
  std::unordered_set<slang::BufferID> open_buffers_;

  // ASIO executor and strand for concurrency control
  asio::any_io_executor executor_;
  asio::strand<asio::any_io_executor> strand_;
};

}  // namespace slangd

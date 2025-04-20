#pragma once

#include <expected>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <asio.hpp>
#include <slang/ast/Compilation.h>
#include <slang/driver/SourceLoader.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/text/SourceManager.h>
#include <spdlog/spdlog.h>

#include "lsp/workspace.hpp"
#include "slangd/core/config_manager.hpp"

namespace slangd {

class WorkspaceManager {
 public:
  WorkspaceManager(
      asio::any_io_executor executor, std::string workspace_folder,
      std::shared_ptr<ConfigManager> config_manager,
      std::shared_ptr<spdlog::logger> logger = nullptr);

  // Scan the workspace to find and process all SystemVerilog files
  auto ScanWorkspace() -> asio::awaitable<void>;

  // Handle LSP file change events
  auto HandleFileChanges(std::vector<lsp::FileEvent> changes)
      -> asio::awaitable<void>;

  // Get the compilation for this workspace
  [[nodiscard]] auto GetCompilation() const
      -> std::shared_ptr<slang::ast::Compilation> {
    return compilation_;
  }

  // Get the source manager
  [[nodiscard]] auto GetSourceManager() const
      -> std::shared_ptr<slang::SourceManager> {
    return source_manager_;
  }

  // Output debugging statistics for the workspace
  auto DumpWorkspaceStats() -> void;

 private:
  // Process a list of source files to create syntax trees and compilation
  auto IndexFiles(std::vector<std::string> file_paths) -> asio::awaitable<void>;

  // Parse a single file
  auto ParseFile(std::string path)
      -> asio::awaitable<std::shared_ptr<slang::syntax::SyntaxTree>>;

  // Event handlers for file changes
  auto HandleFileCreated(std::string path) -> asio::awaitable<void>;
  auto HandleFileChanged(std::string path) -> asio::awaitable<void>;
  auto HandleFileDeleted(std::string path) -> asio::awaitable<void>;

  // Rebuild the workspace compilation after file changes
  auto RebuildWorkspaceCompilation() -> asio::awaitable<void>;

  // Logger instance
  std::shared_ptr<spdlog::logger> logger_;

  // Workspace folder path
  std::string workspace_folder_;

  // Source manager for all workspace files
  std::shared_ptr<slang::SourceManager> source_manager_;

  // Source loader for files in this workspace
  std::unique_ptr<slang::driver::SourceLoader> source_loader_;

  // Map of file paths to their syntax trees
  std::map<std::string, std::shared_ptr<slang::syntax::SyntaxTree>>
      syntax_trees_;

  // The compilation for this workspace
  std::shared_ptr<slang::ast::Compilation> compilation_;

  // The configuration manager
  std::shared_ptr<ConfigManager> config_manager_;

  // ASIO executor and strand for concurrency control
  asio::any_io_executor executor_;
  asio::strand<asio::any_io_executor> strand_;
};

}  // namespace slangd

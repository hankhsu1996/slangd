#pragma once

#include <expected>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <asio.hpp>
#include <slang/ast/Compilation.h>
#include <slang/driver/SourceLoader.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/text/SourceManager.h>
#include <spdlog/spdlog.h>

#include "lsp/workspace.hpp"

namespace slangd {

class WorkspaceManager {
 public:
  explicit WorkspaceManager(
      asio::any_io_executor executor,
      std::shared_ptr<spdlog::logger> logger = nullptr);

  auto Logger() -> std::shared_ptr<spdlog::logger> {
    return logger_;
  }

  // Add a workspace folder URI
  void AddWorkspaceFolder(const std::string& uri, const std::string& name);

  // Scan all workspace folders for SystemVerilog files and build the
  // compilation
  auto ScanWorkspace() -> asio::awaitable<void>;

  // Handle file changes from workspace file watcher
  auto HandleFileChanges(std::vector<lsp::FileEvent> changes)
      -> asio::awaitable<void>;

  // Get workspace folders
  auto GetWorkspaceFolders() const -> const std::vector<std::string>& {
    return workspace_folders_;
  }

  // Get the shared source manager
  auto GetSourceManager() const -> std::shared_ptr<slang::SourceManager> {
    return source_manager_;
  }

  // Get the compilation
  auto GetCompilation() const -> std::shared_ptr<slang::ast::Compilation> {
    return compilation_;
  }

 private:
  // Logger
  std::shared_ptr<spdlog::logger> logger_;

  // Store workspace folder local paths
  std::vector<std::string> workspace_folders_;

  // Find all SystemVerilog files in a directory recursively
  auto FindSystemVerilogFilesInDirectory(std::string directory)
      -> asio::awaitable<std::vector<std::string>>;

  // Index files and build compilation
  auto IndexFiles(std::vector<std::string> file_paths) -> asio::awaitable<void>;

  // Parse a single file and return its syntax tree
  auto ParseFile(std::string path)
      -> asio::awaitable<std::shared_ptr<slang::syntax::SyntaxTree>>;

  // Handle a created file
  auto HandleFileCreated(std::string path) -> asio::awaitable<void>;

  // Handle a changed file
  auto HandleFileChanged(std::string path) -> asio::awaitable<void>;

  // Handle a deleted file
  auto HandleFileDeleted(std::string path) -> asio::awaitable<void>;

  // Rebuild compilation after file changes
  auto RebuildWorkspaceCompilation() -> asio::awaitable<void>;

  // Dump workspace stats
  auto DumpWorkspaceStats() -> void;

  // Map of file path to syntax tree
  std::unordered_map<std::string, std::shared_ptr<slang::syntax::SyntaxTree>>
      syntax_trees_;

  // Source manager for tracking all source buffers
  std::shared_ptr<slang::SourceManager> source_manager_;

  // Source loader for loading and parsing files
  std::unique_ptr<slang::driver::SourceLoader> source_loader_;

  // Global compilation for workspace-wide symbol resolution
  std::shared_ptr<slang::ast::Compilation> compilation_;

  // ASIO executor and strand for synchronization
  asio::any_io_executor executor_;
  asio::strand<asio::any_io_executor> strand_;
};

}  // namespace slangd

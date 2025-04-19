#pragma once

#include <expected>
#include <memory>
#include <optional>
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
#include "slangd/core/slangd_config_file.hpp"

namespace slangd {

class WorkspaceManager {
 public:
  // Constructor
  explicit WorkspaceManager(
      asio::any_io_executor executor,
      std::shared_ptr<spdlog::logger> logger = nullptr);

  // Logger accessor
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
  // Configuration file handling
  auto TryLoadConfigFile(const std::string& workspace_path) -> bool;
  auto IsUsingConfigFile() const -> bool {
    return using_config_file_;
  }
  [[nodiscard]] static auto IsSlangdConfigFile(const std::string& path) -> bool;
  auto GetSourceFilesFromConfig() -> std::vector<std::string>;
  auto ProcessFileList(const std::string& path, bool absolute)
      -> std::vector<std::string>;
  auto HandleConfigFileChange(std::string config_path) -> asio::awaitable<void>;

  // File discovery and indexing
  auto GetWorkspaceSourceFiles() -> asio::awaitable<std::vector<std::string>>;
  auto FindSystemVerilogFilesInDirectory(std::string directory)
      -> asio::awaitable<std::vector<std::string>>;
  auto IndexFiles(std::vector<std::string> file_paths) -> asio::awaitable<void>;
  auto ParseFile(std::string path)
      -> asio::awaitable<std::shared_ptr<slang::syntax::SyntaxTree>>;

  // File change handlers
  auto HandleFileCreated(std::string path) -> asio::awaitable<void>;
  auto HandleFileChanged(std::string path) -> asio::awaitable<void>;
  auto HandleFileDeleted(std::string path) -> asio::awaitable<void>;
  auto RebuildWorkspaceCompilation() -> asio::awaitable<void>;

  // Utility
  auto DumpWorkspaceStats() -> void;

  // Logger
  std::shared_ptr<spdlog::logger> logger_;

  // Store workspace folder local paths
  std::vector<std::string> workspace_folders_;

  // Map of file path to syntax tree
  std::unordered_map<std::string, std::shared_ptr<slang::syntax::SyntaxTree>>
      syntax_trees_;

  // Source manager for tracking all source buffers
  std::shared_ptr<slang::SourceManager> source_manager_;

  // Source loader for loading and parsing files
  std::unique_ptr<slang::driver::SourceLoader> source_loader_;

  // Global compilation for workspace-wide symbol resolution
  std::shared_ptr<slang::ast::Compilation> compilation_;

  // Config file state
  std::optional<SlangdConfigFile> config_;
  bool using_config_file_ = false;

  // ASIO executor and strand for synchronization
  asio::any_io_executor executor_;
  asio::strand<asio::any_io_executor> strand_;
};

}  // namespace slangd

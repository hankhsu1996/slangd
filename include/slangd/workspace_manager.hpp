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

#include "slangd/error/error.hpp"

namespace slangd {

class WorkspaceManager {
 public:
  WorkspaceManager(asio::any_io_executor executor);

  // Add a workspace folder URI
  void AddWorkspaceFolder(const std::string& uri, const std::string& name);

  // Get workspace folders
  const std::vector<std::string>& GetWorkspaceFolders() const;

  // Scan all workspace folders for SystemVerilog files and build the
  // compilation
  asio::awaitable<void> ScanWorkspace();

  // Get the number of indexed files
  size_t GetIndexedFileCount() const;

  // Get the shared source manager
  std::shared_ptr<slang::SourceManager> GetSourceManager() const {
    return source_manager_;
  }

  // Get the compilation
  std::shared_ptr<slang::ast::Compilation> GetCompilation() const {
    return compilation_;
  }

 private:
  // Store workspace folder local paths
  std::vector<std::string> workspace_folders_;

  // Find all SystemVerilog files in a directory recursively
  asio::awaitable<std::vector<std::string>> FindSystemVerilogFiles(
      const std::string& directory);

  // Process collected files and build compilation
  asio::awaitable<std::expected<void, SlangdError>> ProcessFiles(
      const std::vector<std::string>& file_paths);

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

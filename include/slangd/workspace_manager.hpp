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
  explicit WorkspaceManager(asio::any_io_executor executor);

  // Add a workspace folder URI
  void AddWorkspaceFolder(const std::string& uri, const std::string& name);

  // Get workspace folders
  auto GetWorkspaceFolders() const -> const std::vector<std::string>&;

  // Scan all workspace folders for SystemVerilog files and build the
  // compilation
  auto ScanWorkspace() -> asio::awaitable<void>;

  // Get the number of indexed files
  auto GetIndexedFileCount() const -> size_t;

  // Get the shared source manager
  auto GetSourceManager() const -> std::shared_ptr<slang::SourceManager> {
    return source_manager_;
  }

  // Get the compilation
  auto GetCompilation() const -> std::shared_ptr<slang::ast::Compilation> {
    return compilation_;
  }

 private:
  // Store workspace folder local paths
  std::vector<std::string> workspace_folders_;

  // Find all SystemVerilog files in a directory recursively
  auto static FindSystemVerilogFiles(std::string directory)
      -> asio::awaitable<std::vector<std::string>>;

  // Process collected files and build compilation
  auto ProcessFiles(std::vector<std::string> file_paths)
      -> asio::awaitable<std::expected<void, SlangdError>>;

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

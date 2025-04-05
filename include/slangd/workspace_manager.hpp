#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <asio.hpp>
#include <slang/syntax/SyntaxTree.h>

namespace slangd {

class WorkspaceManager {
 public:
  WorkspaceManager(asio::any_io_executor executor);

  // Add a workspace folder URI
  void AddWorkspaceFolder(const std::string& uri, const std::string& name);

  // Get workspace folders
  const std::vector<std::string>& GetWorkspaceFolders() const;

  // Scan all workspace folders for SystemVerilog files
  asio::awaitable<void> ScanWorkspace();

  // Add a file to the workspace index
  asio::awaitable<void> AddFile(const std::string& path);

  // Get the number of indexed files
  size_t GetIndexedFileCount() const;

 private:
  // Store workspace folder local paths
  std::vector<std::string> workspace_folders_;

  // Find all SystemVerilog files in a directory recursively
  asio::awaitable<std::vector<std::string>> FindSystemVerilogFiles(
      const std::string& directory);

  // Map of file path to syntax tree
  std::unordered_map<std::string, std::shared_ptr<slang::syntax::SyntaxTree>>
      syntax_trees_;

  // ASIO executor and strand for synchronization
  asio::any_io_executor executor_;
  asio::strand<asio::any_io_executor> strand_;
};

}  // namespace slangd

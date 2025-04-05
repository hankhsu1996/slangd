#pragma once

#include <string>
#include <vector>

#include <asio.hpp>

namespace slangd {

class WorkspaceManager {
 public:
  WorkspaceManager(asio::any_io_executor executor);

  void AddWorkspaceFolder(const std::string& uri, const std::string& name);

  const std::vector<std::string>& GetWorkspaceFolders() const;

 private:
  asio::any_io_executor executor_;

  std::vector<std::string> workspace_folders_;
};

}  // namespace slangd

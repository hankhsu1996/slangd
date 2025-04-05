#include "slangd/workspace_manager.hpp"

#include <filesystem>

#include <spdlog/spdlog.h>

#include "slangd/utils/uri.hpp"

namespace slangd {

WorkspaceManager::WorkspaceManager(asio::any_io_executor executor)
    : executor_(executor) {}

void WorkspaceManager::AddWorkspaceFolder(
    const std::string& uri, const std::string& name) {
  if (!IsFileUri(uri)) {
    spdlog::warn(
        "WorkspaceManager skipping non-file URI workspace folder: {}", uri);
    return;
  }

  std::string local_path = UriToPath(uri);

  if (!std::filesystem::exists(local_path)) {
    spdlog::warn(
        "WorkspaceManager skipping non-existent workspace folder: {}",
        local_path);
    return;
  }

  spdlog::debug(
      "WorkspaceManager adding workspace folder: {} ({})", name, local_path);
  workspace_folders_.push_back(local_path);
}

const std::vector<std::string>& WorkspaceManager::GetWorkspaceFolders() const {
  return workspace_folders_;
}

}  // namespace slangd

#include "slangd/workspace_manager.hpp"

#include <filesystem>

#include <spdlog/spdlog.h>

#include "slangd/utils/uri.hpp"

namespace slangd {

WorkspaceManager::WorkspaceManager(asio::any_io_executor executor)
    : executor_(executor), strand_(asio::make_strand(executor)) {}

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

asio::awaitable<void> WorkspaceManager::ScanWorkspace() {
  spdlog::info(
      "WorkspaceManager starting workspace scan for SystemVerilog files");

  // Process each workspace folder
  for (const auto& folder : workspace_folders_) {
    spdlog::info("WorkspaceManager scanning workspace folder: {}", folder);
    auto files = co_await FindSystemVerilogFiles(folder);
    spdlog::info(
        "WorkspaceManager found {} SystemVerilog files in {}", files.size(),
        folder);

    // Process files (placeholder - will add batching later)
    for (const auto& file : files) {
      co_await AddFile(file);
    }
  }

  spdlog::info(
      "WorkspaceManager workspace scan completed. Total indexed files: {}",
      GetIndexedFileCount());
  co_return;
}

asio::awaitable<std::vector<std::string>>
WorkspaceManager::FindSystemVerilogFiles(const std::string& directory) {
  std::vector<std::string> sv_files;

  // TODO: Implement actual file scanning
  // This is a placeholder that will be replaced with actual implementation
  spdlog::debug(
      "WorkspaceManager placeholder: Scanning directory {}", directory);

  try {
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(directory)) {
      if (entry.is_regular_file()) {
        std::string ext = entry.path().extension().string();
        if (ext == ".sv" || ext == ".svh" || ext == ".v" || ext == ".vh") {
          sv_files.push_back(entry.path().string());
        }
      }
    }
  } catch (const std::exception& e) {
    spdlog::error("Error scanning directory {}: {}", directory, e.what());
  }

  co_return sv_files;
}

asio::awaitable<void> WorkspaceManager::AddFile(const std::string& path) {
  // TODO: Implement actual file processing
  // This is a placeholder that will be replaced with actual implementation
  spdlog::debug(
      "WorkspaceManager placeholder: Adding file to workspace index: {}", path);

  // In the actual implementation, we would:
  // 1. Read the file content
  // 2. Create a syntax tree
  // 3. Add it to the global compilation

  co_return;
}

size_t WorkspaceManager::GetIndexedFileCount() const {
  return syntax_trees_.size();
}

}  // namespace slangd

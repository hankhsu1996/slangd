#include "slangd/core/discovery_provider.hpp"

#include <filesystem>
#include <fstream>

#include "slangd/utils/path_utils.hpp"

namespace slangd {

// FilelistProvider implementation

FilelistProvider::FilelistProvider(std::shared_ptr<spdlog::logger> logger)
    : logger_(logger ? logger : spdlog::default_logger()) {
}

auto FilelistProvider::DiscoverFiles(
    const CanonicalPath& workspace_root, const SlangdConfigFile& config) const
    -> std::vector<CanonicalPath> {
  std::vector<CanonicalPath> all_files;

  // Add individual files specified in config
  const auto& config_files = config.GetFiles();
  for (const auto& file : config_files) {
    all_files.push_back(file);
  }

  // Process filelist files
  const auto& file_lists = config.GetFileLists();
  for (const auto& rel_path : file_lists.paths) {
    CanonicalPath resolved_path =
        file_lists.absolute ? CanonicalPath(rel_path)
                            : CanonicalPath(workspace_root.Path() / rel_path);

    auto files_from_list = ProcessFileList(resolved_path, file_lists.absolute);
    for (const auto& file : files_from_list) {
      all_files.push_back(file);
    }
  }

  logger_->debug("FilelistProvider discovered {} files", all_files.size());
  return all_files;
}

auto FilelistProvider::ProcessFileList(
    const CanonicalPath& filelist_path, bool absolute) const
    -> std::vector<CanonicalPath> {
  std::vector<CanonicalPath> files;

  std::ifstream file(filelist_path.Path());
  if (!file) {
    logger_->warn(
        "FilelistProvider failed to read filelist: {}", filelist_path.String());
    return files;
  }

  std::string line;
  std::string accumulated_line;
  while (std::getline(file, line)) {
    // Remove leading/trailing whitespace
    while (!line.empty() && (std::isspace(line.front()) != 0)) {
      line.erase(0, 1);
    }
    while (!line.empty() && (std::isspace(line.back()) != 0)) {
      line.pop_back();
    }

    // Skip empty lines and comments
    if (line.empty() || line[0] == '#' || line[0] == '/') {
      continue;
    }

    // Handle line continuation
    if (!line.empty() && line.back() == '\\') {
      accumulated_line += line.substr(0, line.size() - 1);
      continue;
    }

    // Process the complete line
    accumulated_line += line;
    if (!accumulated_line.empty()) {
      CanonicalPath full_path;
      if (absolute) {
        full_path = CanonicalPath(accumulated_line);
      } else {
        auto filelist_dir = CanonicalPath(filelist_path.Path().parent_path());
        full_path = filelist_dir / accumulated_line;
      }
      files.push_back(full_path);
    }

    accumulated_line.clear();
  }

  return files;
}

// WorkspaceDiscoveryProvider implementation

WorkspaceDiscoveryProvider::WorkspaceDiscoveryProvider(
    std::shared_ptr<spdlog::logger> logger)
    : logger_(logger ? logger : spdlog::default_logger()) {
}

auto WorkspaceDiscoveryProvider::DiscoverFiles(
    const CanonicalPath& workspace_root,
    const SlangdConfigFile& /*config*/) const -> std::vector<CanonicalPath> {
  logger_->debug(
      "WorkspaceDiscoveryProvider discovering files in workspace: {}",
      workspace_root.String());

  auto files = FindSystemVerilogFilesInDirectory(workspace_root);
  logger_->debug(
      "WorkspaceDiscoveryProvider discovered {} files", files.size());

  return files;
}

auto WorkspaceDiscoveryProvider::FindSystemVerilogFilesInDirectory(
    const CanonicalPath& directory) const -> std::vector<CanonicalPath> {
  std::vector<CanonicalPath> sv_files;

  logger_->debug(
      "WorkspaceDiscoveryProvider discovering files in directory: {}",
      directory.String());

  try {
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(directory.Path())) {
      if (entry.is_regular_file() && IsSystemVerilogFile(entry.path())) {
        sv_files.emplace_back(entry.path());  // CanonicalPath will normalize
      }
    }
  } catch (const std::exception& e) {
    logger_->error(
        "WorkspaceDiscoveryProvider error discovering files in directory {}: "
        "{}",
        directory.String(), e.what());
  }

  return sv_files;
}

}  // namespace slangd

#include "slangd/core/project_layout_builder.hpp"

#include <unordered_set>

#include "slangd/utils/scoped_timer.hpp"

namespace slangd {

ProjectLayoutBuilder::ProjectLayoutBuilder(
    std::shared_ptr<FilelistProvider> filelist_provider,
    std::shared_ptr<WorkspaceDiscoveryProvider> workspace_discovery_provider,
    std::shared_ptr<spdlog::logger> logger)
    : filelist_provider_(std::move(filelist_provider)),
      workspace_discovery_provider_(std::move(workspace_discovery_provider)),
      logger_(logger ? logger : spdlog::default_logger()) {
}

auto ProjectLayoutBuilder::BuildFromConfig(
    const CanonicalPath& workspace_root, const SlangdConfigFile& config) const
    -> ProjectLayout {
  // Collect files from all sources (additive approach)
  std::vector<CanonicalPath> all_files;

  // 1. Auto-discover workspace files if enabled
  if (config.GetAutoDiscover()) {
    logger_->debug("ProjectLayoutBuilder: auto-discovery enabled");
    auto workspace_files =
        workspace_discovery_provider_->DiscoverFiles(workspace_root, config);
    all_files.insert(
        all_files.end(), workspace_files.begin(), workspace_files.end());
    logger_->debug(
        "ProjectLayoutBuilder: discovered {} files from workspace",
        workspace_files.size());
  }

  // 2. Add files from FileLists and Files sections
  auto explicit_files =
      filelist_provider_->DiscoverFiles(workspace_root, config);
  all_files.insert(
      all_files.end(), explicit_files.begin(), explicit_files.end());
  if (!explicit_files.empty()) {
    logger_->debug(
        "ProjectLayoutBuilder: added {} files from Files/FileLists",
        explicit_files.size());
  }

  // 3. Deduplicate files
  std::unordered_set<CanonicalPath> unique_files(
      all_files.begin(), all_files.end());
  all_files.assign(unique_files.begin(), unique_files.end());
  logger_->debug(
      "ProjectLayoutBuilder: total unique files before filtering: {}",
      all_files.size());

  // 4. Apply path filtering (PathMatch/PathExclude from If block)
  size_t original_count = all_files.size();
  std::vector<CanonicalPath> filtered_files;
  {
    utils::ScopedTimer filter_timer("Path filtering", logger_);
    for (const auto& file : all_files) {
      // Compute relative path from workspace root
      auto relative =
          std::filesystem::relative(file.Path(), workspace_root.Path());

      // Normalize to forward slashes for cross-platform consistency
      auto relative_str = relative.string();
      std::ranges::replace(relative_str, '\\', '/');

      // Check if file should be included based on config conditions
      if (config.ShouldIncludeFile(relative_str)) {
        filtered_files.push_back(file);
      }
    }
  }

  if (filtered_files.size() != original_count) {
    logger_->info(
        "ProjectLayoutBuilder filtered {} files (PathMatch/PathExclude), {} "
        "remaining",
        original_count - filtered_files.size(), filtered_files.size());
  }

  // Extract other configuration data
  auto include_dirs = config.GetIncludeDirs();
  auto defines = config.GetDefines();

  logger_->debug(
      "ProjectLayoutBuilder built layout with {} files, {} includes, {} "
      "defines",
      filtered_files.size(), include_dirs.size(), defines.size());

  return {
      std::move(filtered_files), std::move(include_dirs), std::move(defines)};
}

}  // namespace slangd

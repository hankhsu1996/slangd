#include "slangd/core/project_layout_builder.hpp"

namespace slangd {

ProjectLayoutBuilder::ProjectLayoutBuilder(
    std::shared_ptr<ConfigReader> config_reader,
    std::shared_ptr<FilelistProvider> filelist_provider,
    std::shared_ptr<RepoScanProvider> repo_scan_provider,
    std::shared_ptr<spdlog::logger> logger)
    : config_reader_(std::move(config_reader)),
      filelist_provider_(std::move(filelist_provider)),
      repo_scan_provider_(std::move(repo_scan_provider)),
      logger_(logger ? logger : spdlog::default_logger()) {
}

auto ProjectLayoutBuilder::BuildFromWorkspace(
    const CanonicalPath& workspace_root) const -> ProjectLayout {
  logger_->debug(
      "ProjectLayoutBuilder building layout for workspace: {}", workspace_root);

  // Try to load configuration from workspace
  auto config = config_reader_->LoadFromWorkspace(workspace_root);

  if (config.has_value()) {
    logger_->debug("ProjectLayoutBuilder using loaded config");
    return BuildFromConfig(workspace_root, config.value());
  }
  logger_->debug(
      "ProjectLayoutBuilder no config found, using repo scan fallback");
  // Create empty config for repo scanning
  SlangdConfigFile empty_config;
  return BuildFromConfig(workspace_root, empty_config);
}

auto ProjectLayoutBuilder::BuildFromConfig(
    const CanonicalPath& workspace_root, const SlangdConfigFile& config) const
    -> ProjectLayout {
  // Choose appropriate discovery provider
  auto provider = ChooseDiscoveryProvider(config);

  // Discover files using the chosen provider
  auto discovered_files = provider->DiscoverFiles(workspace_root, config);

  // Apply path filtering (PathMatch/PathExclude from If block)
  size_t original_count = discovered_files.size();
  std::vector<CanonicalPath> filtered_files;
  for (const auto& file : discovered_files) {
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

auto ProjectLayoutBuilder::ChooseDiscoveryProvider(
    const SlangdConfigFile& config) const
    -> std::shared_ptr<DiscoveryProviderBase> {
  // If config has file sources (files or filelists), use FilelistProvider
  if (config.HasFileSources()) {
    logger_->debug("ProjectLayoutBuilder using FilelistProvider");
    return filelist_provider_;
  }
  logger_->debug("ProjectLayoutBuilder using RepoScanProvider");
  return repo_scan_provider_;
}

}  // namespace slangd

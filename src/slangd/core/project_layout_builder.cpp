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

  // Extract other configuration data
  auto include_dirs = config.GetIncludeDirs();
  auto defines = config.GetDefines();

  logger_->debug(
      "ProjectLayoutBuilder built layout with {} files, {} includes, {} "
      "defines",
      discovered_files.size(), include_dirs.size(), defines.size());

  return {
      std::move(discovered_files), std::move(include_dirs), std::move(defines)};
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

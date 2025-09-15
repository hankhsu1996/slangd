#pragma once

#include <memory>

#include <spdlog/spdlog.h>

#include "slangd/core/config_reader.hpp"
#include "slangd/core/discovery_provider.hpp"
#include "slangd/core/project_layout.hpp"
#include "slangd/utils/canonical_path.hpp"

namespace slangd {

// ProjectLayoutBuilder orchestrates the creation of a ProjectLayout
// by combining configuration reading and file discovery.
// It coordinates ConfigReader and DiscoveryProvider to produce
// the final normalized ProjectLayout.
class ProjectLayoutBuilder {
 public:
  // Constructor with dependencies and optional logger
  ProjectLayoutBuilder(
      std::shared_ptr<ConfigReader> config_reader,
      std::shared_ptr<FilelistProvider> filelist_provider,
      std::shared_ptr<RepoScanProvider> repo_scan_provider,
      std::shared_ptr<spdlog::logger> logger = nullptr);

  // Build ProjectLayout from workspace configuration
  // This is the main entry point that coordinates all the components
  [[nodiscard]] auto BuildFromWorkspace(
      const CanonicalPath& workspace_root) const -> ProjectLayout;

  // Build ProjectLayout from explicit config file
  [[nodiscard]] auto BuildFromConfig(
      const CanonicalPath& workspace_root, const SlangdConfigFile& config) const
      -> ProjectLayout;

 private:
  // Choose the appropriate discovery provider based on config
  [[nodiscard]] auto ChooseDiscoveryProvider(const SlangdConfigFile& config)
      const -> std::shared_ptr<DiscoveryProviderBase>;

  // Dependencies
  std::shared_ptr<ConfigReader> config_reader_;
  std::shared_ptr<FilelistProvider> filelist_provider_;
  std::shared_ptr<RepoScanProvider> repo_scan_provider_;

  // Logger
  std::shared_ptr<spdlog::logger> logger_;
};

}  // namespace slangd

#pragma once

#include <memory>

#include <spdlog/spdlog.h>

#include "slangd/core/discovery_provider.hpp"
#include "slangd/core/project_layout.hpp"
#include "slangd/utils/canonical_path.hpp"

namespace slangd {

// ProjectLayoutBuilder orchestrates the creation of a ProjectLayout
// by combining configuration and file discovery.
// It coordinates DiscoveryProviders to produce the final normalized
// ProjectLayout from a given config.
class ProjectLayoutBuilder {
 public:
  // Constructor with dependencies and optional logger
  ProjectLayoutBuilder(
      std::shared_ptr<FilelistProvider> filelist_provider,
      std::shared_ptr<WorkspaceDiscoveryProvider> workspace_discovery_provider,
      std::shared_ptr<spdlog::logger> logger = nullptr);

  // Build ProjectLayout from config file
  [[nodiscard]] auto BuildFromConfig(
      const CanonicalPath& workspace_root, const SlangdConfigFile& config) const
      -> ProjectLayout;

 private:
  // Dependencies
  std::shared_ptr<FilelistProvider> filelist_provider_;
  std::shared_ptr<WorkspaceDiscoveryProvider> workspace_discovery_provider_;

  // Logger
  std::shared_ptr<spdlog::logger> logger_;
};

}  // namespace slangd

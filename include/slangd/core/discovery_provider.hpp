#pragma once

#include <memory>
#include <vector>

#include <spdlog/spdlog.h>

#include "slangd/core/slangd_config_file.hpp"
#include "slangd/utils/canonical_path.hpp"

namespace slangd {

// DiscoveryProviderBase defines the interface for discovering source files
// based on configuration. This allows different discovery strategies:
// - FilelistProvider: reads filelists specified in config
// - RepoScanProvider: scans repository when no filelist is provided
class DiscoveryProviderBase {
 public:
  DiscoveryProviderBase() = default;
  DiscoveryProviderBase(const DiscoveryProviderBase&) = default;
  DiscoveryProviderBase(DiscoveryProviderBase&&) = delete;
  auto operator=(const DiscoveryProviderBase&)
      -> DiscoveryProviderBase& = default;
  auto operator=(DiscoveryProviderBase&&) -> DiscoveryProviderBase& = delete;
  virtual ~DiscoveryProviderBase() = default;

  // Discover source files based on config and workspace root
  // Returns a vector of discovered file paths
  [[nodiscard]] virtual auto DiscoverFiles(
      const CanonicalPath& workspace_root, const SlangdConfigFile& config) const
      -> std::vector<CanonicalPath> = 0;
};

// FilelistProvider reads filelist files specified in the configuration
class FilelistProvider : public DiscoveryProviderBase {
 public:
  explicit FilelistProvider(std::shared_ptr<spdlog::logger> logger = nullptr);

  [[nodiscard]] auto DiscoverFiles(
      const CanonicalPath& workspace_root, const SlangdConfigFile& config) const
      -> std::vector<CanonicalPath> override;

 private:
  // Process a single filelist file
  [[nodiscard]] auto ProcessFileList(
      const CanonicalPath& filelist_path, bool absolute) const
      -> std::vector<CanonicalPath>;

  std::shared_ptr<spdlog::logger> logger_;
};

// RepoScanProvider scans the repository for SystemVerilog files
// when no explicit filelist is provided
class RepoScanProvider : public DiscoveryProviderBase {
 public:
  explicit RepoScanProvider(std::shared_ptr<spdlog::logger> logger = nullptr);

  [[nodiscard]] auto DiscoverFiles(
      const CanonicalPath& workspace_root, const SlangdConfigFile& config) const
      -> std::vector<CanonicalPath> override;

 private:
  // Recursively find SystemVerilog files in a directory
  [[nodiscard]] auto FindSystemVerilogFilesInDirectory(
      const CanonicalPath& directory) const -> std::vector<CanonicalPath>;

  std::shared_ptr<spdlog::logger> logger_;
};

}  // namespace slangd

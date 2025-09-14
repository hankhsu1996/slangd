#pragma once

#include <memory>
#include <optional>

#include <spdlog/spdlog.h>

#include "slangd/core/slangd_config_file.hpp"
#include "slangd/utils/canonical_path.hpp"

namespace slangd {

// ConfigReader is a stateless utility for reading configuration files.
// It extracts the SlangdConfigFile reading logic from ConfigManager
// to improve separation of concerns.
class ConfigReader {
 public:
  // Constructor with optional logger
  explicit ConfigReader(std::shared_ptr<spdlog::logger> logger = nullptr);

  // Load a configuration from a .slangd file at the specified path
  // Returns std::nullopt if file doesn't exist or has critical parsing errors
  [[nodiscard]] auto LoadFromFile(const CanonicalPath& config_path) const
      -> std::optional<SlangdConfigFile>;

  // Load configuration from workspace root (looks for .slangd file)
  [[nodiscard]] auto LoadFromWorkspace(const CanonicalPath& workspace_root)
      const -> std::optional<SlangdConfigFile>;

 private:
  // Logger for diagnostic messages
  std::shared_ptr<spdlog::logger> logger_;
};

}  // namespace slangd

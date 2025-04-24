#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <asio.hpp>
#include <spdlog/spdlog.h>

#include "slangd/core/slangd_config_file.hpp"
#include "slangd/utils/canonical_path.hpp"

namespace slangd {

class ConfigManager {
 public:
  explicit ConfigManager(
      asio::any_io_executor executor, CanonicalPath workspace_root,
      std::shared_ptr<spdlog::logger> logger = nullptr);

  // Load the config file from the workspace root
  // Returns true if a config was found and loaded
  auto LoadConfig(CanonicalPath workspace_root) -> asio::awaitable<bool>;

  // Handle a change to the config file
  // Returns true if a new valid config was loaded
  auto HandleConfigFileChange(CanonicalPath config_path)
      -> asio::awaitable<bool>;

  // Check if a valid configuration is loaded
  [[nodiscard]] auto HasValidConfig() const -> bool {
    return config_.has_value();
  }

  // Get source files from config or fall back to scanning workspace
  [[nodiscard]] auto GetSourceFiles() -> std::vector<CanonicalPath>;

  // Get include directories from config or fall back to all workspace dirs
  [[nodiscard]] auto GetIncludeDirectories() -> std::vector<CanonicalPath>;

  // Get preprocessor defines from config or empty list
  [[nodiscard]] auto GetDefines() -> std::vector<std::string>;

 private:
  // Process file list from configuration
  [[nodiscard]] auto ProcessFileList(const CanonicalPath& path, bool absolute)
      -> std::vector<CanonicalPath>;

  // Helper to scan for SystemVerilog files
  [[nodiscard]] auto FindSystemVerilogFilesInDirectory(
      const CanonicalPath& directory) -> std::vector<CanonicalPath>;

  // Logger instance
  std::shared_ptr<spdlog::logger> logger_;

  // ASIO executor and strand for synchronization
  asio::any_io_executor executor_;
  asio::strand<asio::any_io_executor> strand_;

  // The loaded configuration (if any)
  std::optional<SlangdConfigFile> config_;

  // Root path of the workspace
  CanonicalPath workspace_root_;
};

}  // namespace slangd

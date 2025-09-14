#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <asio.hpp>
#include <spdlog/spdlog.h>

#include "slangd/core/project_layout.hpp"
#include "slangd/core/project_layout_builder.hpp"
#include "slangd/core/slangd_config_file.hpp"
#include "slangd/utils/canonical_path.hpp"

namespace slangd {

// LayoutSnapshot for caching ProjectLayout with versioning
struct LayoutSnapshot {
  std::shared_ptr<const ProjectLayout> layout;
  std::chrono::steady_clock::time_point timestamp;
  uint64_t version;
};

class ProjectLayoutService {
 public:
  // Factory method - creates all dependencies internally
  static auto Create(
      asio::any_io_executor executor, CanonicalPath workspace_root,
      std::shared_ptr<spdlog::logger> logger = nullptr)
      -> std::shared_ptr<ProjectLayoutService>;

  explicit ProjectLayoutService(
      asio::any_io_executor executor, CanonicalPath workspace_root,
      std::shared_ptr<ProjectLayoutBuilder> layout_builder,
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

  // Rebuild the cached ProjectLayout (triggers config file re-reading)
  auto RebuildLayout() -> void;

  // Get current layout version for testing
  [[nodiscard]] auto GetLayoutVersion() -> uint64_t;

  // Get the current layout snapshot with version information
  [[nodiscard]] auto GetLayoutSnapshot() -> LayoutSnapshot;

 private:
  // Get the current ProjectLayout (rebuilding if needed)
  auto GetCurrentLayout() -> const ProjectLayout&;

  // Logger instance
  std::shared_ptr<spdlog::logger> logger_;

  // ASIO executor and strand for synchronization
  asio::any_io_executor executor_;
  asio::strand<asio::any_io_executor> strand_;

  // The loaded configuration (if any)
  std::optional<SlangdConfigFile> config_;

  // Root path of the workspace
  CanonicalPath workspace_root_;

  // New architecture components
  std::shared_ptr<ProjectLayoutBuilder> layout_builder_;
  std::optional<LayoutSnapshot> cached_layout_;
  uint64_t layout_version_ = 0;
};

}  // namespace slangd

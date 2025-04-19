#pragma once

#include <memory>
#include <optional>
#include <string>

#include <asio.hpp>
#include <spdlog/spdlog.h>

#include "slangd/core/slangd_config_file.hpp"

namespace slangd {

class ConfigManager {
 public:
  explicit ConfigManager(
      asio::any_io_executor executor, std::string workspace_root,
      std::shared_ptr<spdlog::logger> logger = nullptr);

  auto Logger() -> std::shared_ptr<spdlog::logger> {
    return logger_;
  }

  // Load the config file from the workspace root
  // Returns true if a config was found and loaded
  auto LoadConfig(std::string workspace_root) -> asio::awaitable<bool>;

  // Check if the given path is a .slangd config file
  [[nodiscard]] static auto IsConfigFile(const std::string& path) -> bool;

  // Handle a change to the config file
  // Returns true if a new valid config was loaded
  auto HandleConfigFileChange(std::string config_path) -> asio::awaitable<bool>;

  // Get the loaded config file
  [[nodiscard]] auto GetConfig() const
      -> const std::optional<SlangdConfigFile>& {
    return config_;
  }

  // Check if a valid configuration is loaded
  [[nodiscard]] auto HasValidConfig() const -> bool {
    return config_.has_value();
  }

 private:
  // Logger instance
  std::shared_ptr<spdlog::logger> logger_;

  // ASIO executor and strand for synchronization
  asio::any_io_executor executor_;
  asio::strand<asio::any_io_executor> strand_;

  // The loaded configuration (if any)
  std::optional<SlangdConfigFile> config_;

  // Root path of the workspace
  std::string workspace_root_;
};

}  // namespace slangd

#include "slangd/core/config_manager.hpp"

#include <filesystem>

#include <spdlog/spdlog.h>

namespace slangd {

ConfigManager::ConfigManager(
    asio::any_io_executor executor, std::string workspace_root,
    std::shared_ptr<spdlog::logger> logger)
    : logger_(logger ? logger : spdlog::default_logger()),
      executor_(executor),
      strand_(asio::make_strand(executor)),
      workspace_root_(workspace_root) {
}

auto ConfigManager::LoadConfig(std::string workspace_root)
    -> asio::awaitable<bool> {
  // Ensure we're running on the strand for thread safety
  co_await asio::post(strand_, asio::use_awaitable);

  workspace_root_ = workspace_root;
  Logger()->debug(
      "ConfigManager loading config from workspace: {}", workspace_root);

  std::filesystem::path config_path =
      std::filesystem::path(workspace_root) / ".slangd";

  auto loaded_config = SlangdConfigFile::LoadFromFile(config_path, logger_);

  if (loaded_config) {
    config_ = std::move(loaded_config.value());
    Logger()->info(
        "ConfigManager loaded .slangd config file from {}",
        config_path.string());

    // Log the configuration details
    Logger()->debug(
        "  Include directories: {}", config_->GetIncludeDirs().size());
    Logger()->debug("  Defines: {}", config_->GetDefines().size());
    Logger()->debug("  Source files: {}", config_->GetFiles().size());
    Logger()->debug("  File lists: {}", config_->GetFileLists().paths.size());

    co_return true;
  } else {
    Logger()->debug(
        "ConfigManager no .slangd config file found at {}",
        config_path.string());
    co_return false;
  }
}

auto ConfigManager::IsConfigFile(const std::string& path) -> bool {
  return std::filesystem::path(path).filename() == ".slangd";
}

auto ConfigManager::HandleConfigFileChange(std::string config_path)
    -> asio::awaitable<bool> {
  // Ensure we're running on the strand for thread safety
  co_await asio::post(strand_, asio::use_awaitable);

  Logger()->info("ConfigManager reloading config file: {}", config_path);

  // Check if this config belongs to our workspace
  if (!config_path.starts_with(workspace_root_)) {
    Logger()->warn(
        "ConfigManager ignoring config file outside current workspace: {}",
        config_path);
    co_return false;
  }

  // Try to load the updated config
  auto loaded_config = SlangdConfigFile::LoadFromFile(config_path, logger_);

  if (loaded_config) {
    // Update the configuration
    config_ = std::move(loaded_config.value());
    Logger()->info("ConfigManager successfully reloaded configuration");

    // Log the updated configuration details
    Logger()->debug(
        "  Include directories: {}", config_->GetIncludeDirs().size());
    Logger()->debug("  Defines: {}", config_->GetDefines().size());
    Logger()->debug("  Source files: {}", config_->GetFiles().size());
    Logger()->debug("  File lists: {}", config_->GetFileLists().paths.size());

    co_return true;
  } else {
    // Config file was deleted or has errors
    Logger()->info("ConfigManager config file was removed or contains errors");
    config_.reset();  // Clear the configuration
    co_return false;
  }
}

}  // namespace slangd

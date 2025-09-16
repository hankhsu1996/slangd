#include "slangd/core/project_layout_service.hpp"

#include <filesystem>
#include <utility>

#include <spdlog/spdlog.h>

namespace slangd {

auto ProjectLayoutService::Create(
    asio::any_io_executor executor, CanonicalPath workspace_root,
    std::shared_ptr<spdlog::logger> logger)
    -> std::shared_ptr<ProjectLayoutService> {
  auto config_reader = std::make_shared<ConfigReader>(logger);
  auto filelist_provider = std::make_shared<FilelistProvider>(logger);
  auto repo_scan_provider = std::make_shared<RepoScanProvider>(logger);
  auto layout_builder = std::make_shared<ProjectLayoutBuilder>(
      config_reader, filelist_provider, repo_scan_provider, logger);

  return std::make_shared<ProjectLayoutService>(
      executor, workspace_root, layout_builder, logger);
}

ProjectLayoutService::ProjectLayoutService(
    asio::any_io_executor executor, CanonicalPath workspace_root,
    std::shared_ptr<ProjectLayoutBuilder> layout_builder,
    std::shared_ptr<spdlog::logger> logger)
    : logger_(logger ? logger : spdlog::default_logger()),
      executor_(executor),
      strand_(asio::make_strand(executor)),
      workspace_root_(std::move(workspace_root)),
      layout_builder_(std::move(layout_builder)) {
}

auto ProjectLayoutService::LoadConfig(CanonicalPath workspace_root)
    -> asio::awaitable<bool> {
  // Ensure we're running on the strand for thread safety
  co_await asio::post(strand_, asio::use_awaitable);

  workspace_root_ = workspace_root;
  logger_->debug(
      "ConfigManager loading config from workspace: {}", workspace_root_);

  auto config_path = workspace_root_ / ".slangd";

  auto loaded_config = SlangdConfigFile::LoadFromFile(config_path, logger_);

  if (loaded_config) {
    config_ = std::move(loaded_config.value());
    logger_->info(
        "ConfigManager loaded .slangd config file from {}", config_path);

    RebuildLayout();
    co_return true;
  } else {
    logger_->debug(
        "ConfigManager no .slangd config file found at {}", config_path);
    co_return false;
  }
}

auto ProjectLayoutService::HandleConfigFileChange(CanonicalPath config_path)
    -> asio::awaitable<bool> {
  // Ensure we're running on the strand for thread safety
  co_await asio::post(strand_, asio::use_awaitable);

  logger_->info("ConfigManager reloading config file: {}", config_path);

  // Check if this config belongs to our workspace
  if (!workspace_root_.IsSubPathOf(config_path)) {
    logger_->warn(
        "ConfigManager ignoring config file outside current workspace: {}",
        config_path);
    co_return false;
  }

  // Try to load the updated config
  auto loaded_config = SlangdConfigFile::LoadFromFile(config_path, logger_);

  if (loaded_config) {
    // Update the configuration
    config_ = std::move(loaded_config.value());
    logger_->info("ConfigManager successfully reloaded configuration");

    RebuildLayout();
    co_return true;
  } else {
    // Config file was deleted or has errors
    logger_->info("ConfigManager config file was removed or contains errors");
    config_.reset();  // Clear the configuration
    RebuildLayout();  // Rebuild with empty config fallback
    co_return false;
  }
}

auto ProjectLayoutService::RebuildLayout() -> void {
  layout_version_++;
  auto new_layout = layout_builder_->BuildFromWorkspace(workspace_root_);
  cached_layout_ = LayoutSnapshot{
      .layout = std::make_shared<const ProjectLayout>(std::move(new_layout)),
      .timestamp = std::chrono::steady_clock::now(),
      .version = layout_version_};
  logger_->debug("ConfigManager rebuilt layout (version {})", layout_version_);
}

auto ProjectLayoutService::GetCurrentLayout() -> const ProjectLayout& {
  if (!cached_layout_.has_value()) {
    RebuildLayout();
  }
  return *cached_layout_->layout;
}

auto ProjectLayoutService::GetSourceFiles() -> std::vector<CanonicalPath> {
  return GetCurrentLayout().GetFiles();
}

auto ProjectLayoutService::GetIncludeDirectories()
    -> std::vector<CanonicalPath> {
  return GetCurrentLayout().GetIncludeDirs();
}

auto ProjectLayoutService::GetDefines() -> std::vector<std::string> {
  return GetCurrentLayout().GetDefines();
}

auto ProjectLayoutService::GetLayoutVersion() -> uint64_t {
  if (!cached_layout_) {
    // Force layout initialization
    GetCurrentLayout();
  }
  return cached_layout_->version;
}

auto ProjectLayoutService::GetLayoutSnapshot() -> LayoutSnapshot {
  if (!cached_layout_) {
    // Force layout initialization by calling GetCurrentLayout()
    GetCurrentLayout();
  }
  return *cached_layout_;
}

auto ProjectLayoutService::ScheduleDebouncedRebuild() -> void {
  // Check if we're in auto-discovery mode
  if (!IsInAutoDiscoveryMode()) {
    logger_->debug(
        "ProjectLayoutService: In config mode, ignoring file system changes");
    return;
  }

  logger_->debug(
      "ProjectLayoutService: In auto-discovery mode, scheduling debounced "
      "rebuild");

  // Cancel existing timer if any
  if (debounce_timer_) {
    debounce_timer_->cancel();
  }

  // Create new timer
  debounce_timer_ = asio::steady_timer(executor_, kDebounceDelay);
  debounce_timer_->async_wait([this](std::error_code ec) {
    if (!ec) {
      logger_->debug(
          "ProjectLayoutService: Debounce timer expired, performing rebuild");
      RebuildLayout();
      debounce_timer_.reset();
    } else if (ec != asio::error::operation_aborted) {
      logger_->warn(
          "ProjectLayoutService: Debounce timer error: {}", ec.message());
    }
  });
}

auto ProjectLayoutService::IsInAutoDiscoveryMode() const -> bool {
  // Auto-discovery mode when no config OR config has no file sources
  return !config_.has_value() || !config_->HasFileSources();
}

}  // namespace slangd

#include "slangd/core/project_layout_service.hpp"

#include <filesystem>
#include <utility>

#include <spdlog/spdlog.h>

namespace slangd {

auto ProjectLayoutService::Create(
    asio::any_io_executor executor, CanonicalPath workspace_root,
    std::shared_ptr<spdlog::logger> logger)
    -> std::shared_ptr<ProjectLayoutService> {
  auto filelist_provider = std::make_shared<FilelistProvider>(logger);
  auto workspace_discovery_provider =
      std::make_shared<WorkspaceDiscoveryProvider>(logger);
  auto layout_builder = std::make_shared<ProjectLayoutBuilder>(
      filelist_provider, workspace_discovery_provider, logger);

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
  } else {
    // No config file - use default config (AutoDiscover=true)
    config_ = SlangdConfigFile();
    logger_->debug(
        "ConfigManager no .slangd config file found at {}, using defaults",
        config_path);
  }

  RebuildLayout();
  co_return loaded_config.has_value();
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
  } else {
    // Config file was deleted or has errors - reset to default config
    logger_->info(
        "ConfigManager config file was removed or contains errors, using "
        "defaults");
    config_ = SlangdConfigFile();
  }

  RebuildLayout();
  co_return loaded_config.has_value();
}

auto ProjectLayoutService::RebuildLayout() -> void {
  layout_version_++;
  // Use stored config instead of reloading from filesystem
  auto new_layout = layout_builder_->BuildFromConfig(workspace_root_, config_);
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
  // Only watch filesystem when AutoDiscover is enabled
  if (!config_.GetAutoDiscover()) {
    logger_->debug(
        "ProjectLayoutService: AutoDiscover disabled, ignoring file system "
        "changes");
    return;
  }

  logger_->debug(
      "ProjectLayoutService: AutoDiscover enabled, scheduling debounced "
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

auto ProjectLayoutService::AddFile(CanonicalPath path)
    -> asio::awaitable<void> {
  co_await asio::post(strand_, asio::use_awaitable);

  // Only modify layout when AutoDiscover is enabled
  if (!config_.GetAutoDiscover()) {
    logger_->debug(
        "ProjectLayoutService: AutoDiscover disabled, ignoring file addition: "
        "{}",
        path);
    co_return;
  }

  // Compute relative path and normalize to forward slashes
  auto relative =
      std::filesystem::relative(path.Path(), workspace_root_.Path());
  auto relative_str = relative.string();
  std::ranges::replace(relative_str, '\\', '/');

  // Check if file passes filter
  if (!config_.ShouldIncludeFile(relative_str)) {
    logger_->debug(
        "ProjectLayoutService: File filtered out, not adding: {}", path);
    co_return;
  }

  // Get current layout files (require layout to exist)
  if (!cached_layout_) {
    logger_->warn(
        "ProjectLayoutService: Cannot add file, layout not initialized: {}",
        path);
    co_return;
  }

  auto current_files = cached_layout_->layout->GetFiles();

  // Check if file already exists
  if (std::ranges::contains(current_files, path)) {
    logger_->debug(
        "ProjectLayoutService: File already in layout, skipping: {}", path);
    co_return;
  }

  // Add file to layout
  current_files.push_back(path);

  // Create new layout with updated files
  auto include_dirs = cached_layout_->layout->GetIncludeDirs();
  auto defines = cached_layout_->layout->GetDefines();

  layout_version_++;
  cached_layout_ = LayoutSnapshot{
      .layout = std::make_shared<const ProjectLayout>(
          std::move(current_files), std::move(include_dirs),
          std::move(defines)),
      .timestamp = std::chrono::steady_clock::now(),
      .version = layout_version_};

  logger_->debug(
      "ProjectLayoutService: Added file to layout: {} (version {})", path,
      layout_version_);
}

auto ProjectLayoutService::RemoveFile(CanonicalPath path)
    -> asio::awaitable<void> {
  co_await asio::post(strand_, asio::use_awaitable);

  // Get current layout files (require layout to exist)
  if (!cached_layout_) {
    logger_->debug(
        "ProjectLayoutService: Cannot remove file, layout not initialized: {}",
        path);
    co_return;
  }

  auto current_files = cached_layout_->layout->GetFiles();

  // Check if file exists in layout
  auto it = std::ranges::find(current_files, path);
  if (it == current_files.end()) {
    logger_->debug(
        "ProjectLayoutService: File not in layout, skipping removal: {}", path);
    co_return;
  }

  // Remove file from layout
  current_files.erase(it);

  // Create new layout with updated files
  auto include_dirs = cached_layout_->layout->GetIncludeDirs();
  auto defines = cached_layout_->layout->GetDefines();

  layout_version_++;
  cached_layout_ = LayoutSnapshot{
      .layout = std::make_shared<const ProjectLayout>(
          std::move(current_files), std::move(include_dirs),
          std::move(defines)),
      .timestamp = std::chrono::steady_clock::now(),
      .version = layout_version_};

  logger_->debug(
      "ProjectLayoutService: Removed file from layout: {} (version {})", path,
      layout_version_);
}

}  // namespace slangd

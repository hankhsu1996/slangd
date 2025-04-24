#include "slangd/core/config_manager.hpp"

#include <filesystem>
#include <fstream>
#include <utility>

#include <spdlog/spdlog.h>

#include "slangd/utils/path_utils.hpp"

namespace slangd {

ConfigManager::ConfigManager(
    asio::any_io_executor executor, CanonicalPath workspace_root,
    std::shared_ptr<spdlog::logger> logger)
    : logger_(logger ? logger : spdlog::default_logger()),
      executor_(executor),
      strand_(asio::make_strand(executor)),
      workspace_root_(std::move(workspace_root)) {
}

auto ConfigManager::LoadConfig(CanonicalPath workspace_root)
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

    // Log the configuration details
    logger_->debug(
        "  Include directories: {}", config_->GetIncludeDirs().size());
    logger_->debug("  Defines: {}", config_->GetDefines().size());
    logger_->debug("  Source files: {}", config_->GetFiles().size());
    logger_->debug("  File lists: {}", config_->GetFileLists().paths.size());

    co_return true;
  } else {
    logger_->debug(
        "ConfigManager no .slangd config file found at {}", config_path);
    co_return false;
  }
}

auto ConfigManager::HandleConfigFileChange(CanonicalPath config_path)
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

    // Log the updated configuration details
    logger_->debug(
        "  Include directories: {}", config_->GetIncludeDirs().size());
    logger_->debug("  Defines: {}", config_->GetDefines().size());
    logger_->debug("  Source files: {}", config_->GetFiles().size());
    logger_->debug("  File lists: {}", config_->GetFileLists().paths.size());

    co_return true;
  } else {
    // Config file was deleted or has errors
    logger_->info("ConfigManager config file was removed or contains errors");
    config_.reset();  // Clear the configuration
    co_return false;
  }
}
auto ConfigManager::GetSourceFiles() -> std::vector<CanonicalPath> {
  if (HasValidConfig()) {
    const auto& config = config_.value();
    std::vector<CanonicalPath> all_files = config.GetFiles();

    const auto& file_lists = config.GetFileLists();
    for (const auto& rel_path : file_lists.paths) {
      CanonicalPath resolved_path =
          file_lists.absolute
              ? CanonicalPath(rel_path)
              : CanonicalPath(workspace_root_.Path() / rel_path);

      auto files_from_list =
          ProcessFileList(resolved_path, file_lists.absolute);
      for (const auto& f : files_from_list) {
        all_files.emplace_back(f);
      }
    }

    logger_->debug(
        "ConfigManager using {} source files from config file",
        all_files.size());
    return all_files;
  }

  std::vector<CanonicalPath> all_files;
  logger_->debug(
      "ConfigManager scanning workspace folder: {}", workspace_root_.String());

  auto files = FindSystemVerilogFilesInDirectory(workspace_root_);
  for (const auto& f : files) {
    all_files.emplace_back(f);
  }

  logger_->debug(
      "ConfigManager found {} SystemVerilog files in {}", files.size(),
      workspace_root_.String());
  return all_files;
}

auto ConfigManager::GetIncludeDirectories() -> std::vector<CanonicalPath> {
  if (HasValidConfig()) {
    logger_->debug("ConfigManager using include directories from config");
    return config_->GetIncludeDirs();
  }

  logger_->debug("ConfigManager using all workspace directories as fallback");
  std::vector<CanonicalPath> include_dirs;

  try {
    for (const auto& entry : std::filesystem::recursive_directory_iterator(
             workspace_root_.Path())) {
      if (entry.is_directory()) {
        include_dirs.emplace_back(entry.path());
      }
    }
  } catch (const std::exception& e) {
    logger_->error("Error scanning directories: {}", e.what());
  }

  return include_dirs;
}

auto ConfigManager::GetDefines() -> std::vector<std::string> {
  // Use defines from config if available
  if (HasValidConfig()) {
    logger_->debug("ConfigManager using defines from config");
    return config_->GetDefines();
  }
  // Fallback: empty list
  logger_->debug("ConfigManager using empty defines list as fallback");
  return {};
}
auto ConfigManager::ProcessFileList(const CanonicalPath& path, bool absolute)
    -> std::vector<CanonicalPath> {
  std::vector<CanonicalPath> files;

  std::ifstream file(path.Path());
  if (!file) {
    logger_->warn("ConfigManager failed to read filelist: {}", path.String());
    return files;
  }

  std::string line;
  std::string accumulated_line;
  while (std::getline(file, line)) {
    // Remove leading/trailing whitespace
    while (!line.empty() && (std::isspace(line.front()) != 0)) {
      line.erase(0, 1);
    }
    while (!line.empty() && (std::isspace(line.back()) != 0)) {
      line.pop_back();
    }

    // Skip empty lines and comments
    if (line.empty() || line[0] == '#' || line[0] == '/') {
      continue;
    }

    // Handle line continuation
    if (!line.empty() && line.back() == '\\') {
      accumulated_line += line.substr(0, line.size() - 1);
      continue;
    }

    // Process the complete line
    accumulated_line += line;
    if (!accumulated_line.empty()) {
      CanonicalPath full_path;
      if (absolute) {
        full_path = CanonicalPath(accumulated_line);
      } else {
        auto filelist_dir = CanonicalPath(path.Path().parent_path());
        full_path = filelist_dir / accumulated_line;
      }
      files.push_back(full_path);
    }

    accumulated_line.clear();
  }

  return files;
}

auto ConfigManager::FindSystemVerilogFilesInDirectory(
    const CanonicalPath& directory) -> std::vector<CanonicalPath> {
  std::vector<CanonicalPath> sv_files;

  logger_->debug("ConfigManager scanning directory: {}", directory.String());

  try {
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(directory.Path())) {
      if (entry.is_regular_file() && IsSystemVerilogFile(entry.path())) {
        sv_files.emplace_back(entry.path());  // CanonicalPath will normalize
      }
    }
  } catch (const std::exception& e) {
    logger_->error(
        "Error scanning directory {}: {}", directory.String(), e.what());
  }

  return sv_files;
}

}  // namespace slangd

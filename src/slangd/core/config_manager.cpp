#include "slangd/core/config_manager.hpp"

#include <filesystem>
#include <fstream>

#include <spdlog/spdlog.h>

#include "slangd/utils/path_utils.hpp"

namespace slangd {

ConfigManager::ConfigManager(
    asio::any_io_executor executor, std::filesystem::path workspace_root,
    std::shared_ptr<spdlog::logger> logger)
    : logger_(logger ? logger : spdlog::default_logger()),
      executor_(executor),
      strand_(asio::make_strand(executor)),
      workspace_root_(workspace_root) {
}

auto ConfigManager::LoadConfig(std::filesystem::path workspace_root)
    -> asio::awaitable<bool> {
  // Ensure we're running on the strand for thread safety
  co_await asio::post(strand_, asio::use_awaitable);

  workspace_root_ = workspace_root;
  logger_->debug(
      "ConfigManager loading config from workspace: {}",
      workspace_root_.string());

  std::filesystem::path config_path =
      std::filesystem::path(workspace_root) / ".slangd";

  auto loaded_config = SlangdConfigFile::LoadFromFile(config_path, logger_);

  if (loaded_config) {
    config_ = std::move(loaded_config.value());
    logger_->info(
        "ConfigManager loaded .slangd config file from {}",
        config_path.string());

    // Log the configuration details
    logger_->debug(
        "  Include directories: {}", config_->GetIncludeDirs().size());
    logger_->debug("  Defines: {}", config_->GetDefines().size());
    logger_->debug("  Source files: {}", config_->GetFiles().size());
    logger_->debug("  File lists: {}", config_->GetFileLists().paths.size());

    co_return true;
  } else {
    logger_->debug(
        "ConfigManager no .slangd config file found at {}",
        config_path.string());
    co_return false;
  }
}

auto ConfigManager::HandleConfigFileChange(std::filesystem::path config_path)
    -> asio::awaitable<bool> {
  // Ensure we're running on the strand for thread safety
  co_await asio::post(strand_, asio::use_awaitable);

  logger_->info(
      "ConfigManager reloading config file: {}", config_path.string());

  // Check if this config belongs to our workspace
  if (!IsSubPath(workspace_root_, config_path)) {
    logger_->warn(
        "ConfigManager ignoring config file outside current workspace: {}",
        config_path.string());
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

auto ConfigManager::GetSourceFiles() -> std::vector<std::filesystem::path> {
  // Use files from config if available
  if (HasValidConfig()) {
    // Get files from config
    const auto& config = config_.value();

    // Combine the files directly specified in the config
    std::vector<std::filesystem::path> all_files = config.GetFiles();

    // Process file lists if present
    const auto& file_lists = config.GetFileLists();
    for (const auto& file_list_path : file_lists.paths) {
      std::string resolved_path = file_list_path;
      if (!file_lists.absolute && !workspace_root_.empty()) {
        resolved_path =
            (std::filesystem::path(workspace_root_) / file_list_path).string();
      }

      // Process the file list and add files
      auto files_from_list =
          ProcessFileList(resolved_path, file_lists.absolute);
      all_files.insert(
          all_files.end(), files_from_list.begin(), files_from_list.end());
    }

    logger_->debug(
        "ConfigManager using {} source files from config file",
        all_files.size());
    return all_files;
  }

  // Fall back to auto-discovery
  std::vector<std::filesystem::path> all_files;
  logger_->debug(
      "ConfigManager scanning workspace folder: {}", workspace_root_.string());
  auto files = FindSystemVerilogFilesInDirectory(workspace_root_);
  logger_->debug(
      "ConfigManager found {} SystemVerilog files in {}", files.size(),
      workspace_root_.string());
  all_files.insert(all_files.end(), files.begin(), files.end());
  return all_files;
}

auto ConfigManager::GetIncludeDirectories()
    -> std::vector<std::filesystem::path> {
  // Use include directories from config if available
  if (HasValidConfig()) {
    logger_->debug("ConfigManager using include directories from config");
    return config_->GetIncludeDirs();
  }
  // Fallback: scan the workspace for all directories
  logger_->debug("ConfigManager using all workspace directories as fallback");
  std::vector<std::filesystem::path> include_dirs;

  try {
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(workspace_root_)) {
      if (entry.is_directory()) {
        include_dirs.push_back(entry.path());
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

auto ConfigManager::ProcessFileList(
    const std::filesystem::path& path, bool absolute)
    -> std::vector<std::filesystem::path> {
  std::vector<std::filesystem::path> files;

  // Read the filelist content
  std::ifstream file(path);
  if (!file) {
    logger_->warn("ConfigManager failed to read filelist: {}", path.string());
    return files;
  }

  std::string line;
  std::string accumulated_line;
  while (std::getline(file, line)) {
    // Skip empty lines and comments
    // Remove leading/trailing whitespace
    while (!line.empty() && (std::isspace(line.front()) != 0)) {
      line.erase(0, 1);
    }
    while (!line.empty() && (std::isspace(line.back()) != 0)) {
      line.pop_back();
    }

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
      std::string file_path = accumulated_line;
      if (!absolute) {
        auto filelist_dir = std::filesystem::path(path).parent_path();
        file_path = (filelist_dir / file_path).string();
      }
      files.push_back(file_path);
    }
    accumulated_line.clear();
  }

  return files;
}

auto ConfigManager::FindSystemVerilogFilesInDirectory(
    const std::filesystem::path& directory)
    -> std::vector<std::filesystem::path> {
  std::vector<std::filesystem::path> sv_files;

  logger_->debug("ConfigManager scanning directory: {}", directory.string());

  try {
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(directory)) {
      if (entry.is_regular_file()) {
        if (IsSystemVerilogFile(entry.path())) {
          // Normalize path before adding to ensure consistency
          sv_files.push_back(NormalizePath(entry.path()));
        }
      }
    }
  } catch (const std::exception& e) {
    logger_->error(
        "Error scanning directory {}: {}", directory.string(), e.what());
  }

  return sv_files;
}

}  // namespace slangd

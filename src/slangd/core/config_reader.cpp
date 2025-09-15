#include "slangd/core/config_reader.hpp"

namespace slangd {

ConfigReader::ConfigReader(std::shared_ptr<spdlog::logger> logger)
    : logger_(logger ? logger : spdlog::default_logger()) {
}

auto ConfigReader::LoadFromFile(const CanonicalPath& config_path) const
    -> std::optional<SlangdConfigFile> {
  logger_->debug("ConfigReader loading config from: {}", config_path);

  // Delegate to SlangdConfigFile's existing loading logic
  return SlangdConfigFile::LoadFromFile(config_path, logger_);
}

auto ConfigReader::LoadFromWorkspace(const CanonicalPath& workspace_root) const
    -> std::optional<SlangdConfigFile> {
  auto config_path = workspace_root / ".slangd";
  logger_->debug(
      "ConfigReader loading config from workspace: {}", workspace_root);

  return LoadFromFile(config_path);
}

}  // namespace slangd

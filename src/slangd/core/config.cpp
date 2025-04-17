#include "slangd/core/config.hpp"

#include <yaml-cpp/yaml.h>

#include <spdlog/spdlog.h>

namespace slangd {

SlangdConfig::SlangdConfig(std::shared_ptr<spdlog::logger> logger)
    : logger_(logger ? logger : spdlog::default_logger()) {
}

auto SlangdConfig::CreateDefault(std::shared_ptr<spdlog::logger> logger)
    -> SlangdConfig {
  SlangdConfig config(logger);
  // For fallback, we'll add the current directory as an include directory
  config.include_dirs_.emplace_back(".");
  return config;
}

auto SlangdConfig::LoadFromFile(
    const std::filesystem::path& root, std::shared_ptr<spdlog::logger> logger)
    -> std::optional<SlangdConfig> {
  SlangdConfig config(logger);
  auto config_path = root / ".slangd";

  if (!std::filesystem::exists(config_path)) {
    config.logger_->debug(
        "No .slangd configuration file found at {}", config_path.string());
    return std::nullopt;
  }

  try {
    // Load YAML file
    YAML::Node yaml = YAML::LoadFile(config_path.string());

    // Parse FileLists section
    if (yaml["FileLists"]) {
      if (yaml["FileLists"]["Paths"]) {
        for (const auto& path : yaml["FileLists"]["Paths"]) {
          config.file_lists_.paths.push_back(path.as<std::string>());
        }
      }

      if (yaml["FileLists"]["Absolute"]) {
        config.file_lists_.absolute = yaml["FileLists"]["Absolute"].as<bool>();
      }
    }

    // Parse Files section
    if (yaml["Files"]) {
      for (const auto& file : yaml["Files"]) {
        config.files_.push_back(file.as<std::string>());
      }
    }

    // Parse IncludeDirs section
    if (yaml["IncludeDirs"]) {
      for (const auto& dir : yaml["IncludeDirs"]) {
        config.include_dirs_.push_back(dir.as<std::string>());
      }
    }

    // Parse Defines section
    if (yaml["Defines"]) {
      for (const auto& define : yaml["Defines"]) {
        config.defines_.push_back(define.as<std::string>());
      }
    }

    config.logger_->debug(
        "Loaded .slangd configuration from {}", config_path.string());
    return config;

  } catch (const YAML::Exception& e) {
    config.logger_->error(
        "Error parsing .slangd configuration file: {}", e.what());
    return std::nullopt;
  } catch (const std::exception& e) {
    config.logger_->error(
        "Error loading .slangd configuration file: {}", e.what());
    return std::nullopt;
  }
}

}  // namespace slangd

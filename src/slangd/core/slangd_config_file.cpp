#include "slangd/core/slangd_config_file.hpp"

#include <yaml-cpp/yaml.h>

#include <spdlog/spdlog.h>

namespace slangd {

SlangdConfigFile::SlangdConfigFile(std::shared_ptr<spdlog::logger> logger)
    : logger_(logger ? logger : spdlog::default_logger()) {
}

auto SlangdConfigFile::CreateDefault(std::shared_ptr<spdlog::logger> logger)
    -> SlangdConfigFile {
  SlangdConfigFile config(logger);
  // For fallback, we'll add the current directory as an include directory
  config.include_dirs_.emplace_back(std::filesystem::current_path());
  return config;
}

auto SlangdConfigFile::LoadFromFile(
    const CanonicalPath& config_path, std::shared_ptr<spdlog::logger> logger)
    -> std::optional<SlangdConfigFile> {
  SlangdConfigFile config(logger);

  if (!std::filesystem::exists(config_path.Path())) {
    config.logger_->debug(
        "No .slangd configuration file found at {}", config_path);
    return std::nullopt;
  }

  try {
    // Load YAML file
    YAML::Node yaml = YAML::LoadFile(config_path.String());

    // Parse FileLists section
    if (yaml["FileLists"]) {
      if (yaml["FileLists"]["Paths"]) {
        for (const auto& path : yaml["FileLists"]["Paths"]) {
          auto raw = path.as<std::string>();
          config.file_lists_.paths.emplace_back(raw);
        }
      }

      if (yaml["FileLists"]["Absolute"]) {
        config.file_lists_.absolute = yaml["FileLists"]["Absolute"].as<bool>();
      }
    }

    // Parse Files section
    if (yaml["Files"]) {
      for (const auto& file : yaml["Files"]) {
        auto raw = file.as<std::string>();
        config.files_.emplace_back(raw);
      }
    }

    // Parse IncludeDirs section
    if (yaml["IncludeDirs"]) {
      for (const auto& dir : yaml["IncludeDirs"]) {
        auto raw = dir.as<std::string>();
        config.include_dirs_.emplace_back(raw);
      }
    }

    // Parse Defines section
    if (yaml["Defines"]) {
      for (const auto& define : yaml["Defines"]) {
        if (define.IsMap()) {
          // Handle key-value pairs: AHC_INST_ON: 1 -> "AHC_INST_ON=1"
          for (const auto& kv : define) {
            auto key = kv.first.as<std::string>();
            auto value = kv.second.as<std::string>();
            auto define_str = key + "=" + value;
            config.defines_.push_back(define_str);
          }
        } else {
          // Handle simple strings: "DEBUG" -> "DEBUG"
          auto define_str = define.as<std::string>();
          config.defines_.push_back(define_str);
        }
      }
    }

    config.logger_->debug("Loaded .slangd configuration from {}", config_path);
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

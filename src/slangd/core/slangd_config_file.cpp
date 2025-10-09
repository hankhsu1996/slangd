#include "slangd/core/slangd_config_file.hpp"

#include <regex>
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

    // Parse If section (path filtering)
    if (yaml["If"]) {
      if (yaml["If"]["PathMatch"]) {
        config.path_condition_.path_match =
            yaml["If"]["PathMatch"].as<std::string>();
        config.logger_->debug(
            "Loaded PathMatch: {}", *config.path_condition_.path_match);
      }

      if (yaml["If"]["PathExclude"]) {
        config.path_condition_.path_exclude =
            yaml["If"]["PathExclude"].as<std::string>();
        config.logger_->debug(
            "Loaded PathExclude: {}", *config.path_condition_.path_exclude);
      }
    }

    // Parse AutoDiscover section
    if (yaml["AutoDiscover"]) {
      config.auto_discover_ = yaml["AutoDiscover"].as<bool>();
      config.logger_->debug("Loaded AutoDiscover: {}", config.auto_discover_);
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

auto SlangdConfigFile::ShouldIncludeFile(std::string_view relative_path) const
    -> bool {
  // No conditions specified -> include everything
  if (!path_condition_.path_match.has_value() &&
      !path_condition_.path_exclude.has_value()) {
    return true;
  }

  // Convert to string for regex matching (relative_path is already normalized)
  std::string path_str(relative_path);

  try {
    // Check PathMatch: if specified, path must match to be included
    if (path_condition_.path_match.has_value()) {
      std::regex match_pattern(*path_condition_.path_match);
      if (!std::regex_match(path_str, match_pattern)) {
        return false;  // Doesn't match PathMatch -> exclude
      }
    }

    // Check PathExclude: if specified and matches, exclude
    if (path_condition_.path_exclude.has_value()) {
      std::regex exclude_pattern(*path_condition_.path_exclude);
      if (std::regex_match(path_str, exclude_pattern)) {
        return false;  // Matches PathExclude -> exclude
      }
    }

    // Passed all conditions -> include
    return true;

  } catch (const std::regex_error& e) {
    logger_->warn(
        "Invalid regex in path condition ({}), including file by default: {}",
        e.what(), relative_path);
    return true;  // Fail open on regex error
  }
}

}  // namespace slangd

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
        const auto& match_node = yaml["If"]["PathMatch"];
        if (match_node.IsScalar()) {
          // Single pattern
          config.path_condition_.path_match.push_back(
              match_node.as<std::string>());
          config.logger_->debug(
              "Loaded PathMatch: {}", config.path_condition_.path_match[0]);
        } else if (match_node.IsSequence()) {
          // List of patterns
          for (const auto& pattern : match_node) {
            config.path_condition_.path_match.push_back(
                pattern.as<std::string>());
          }
          config.logger_->debug(
              "Loaded PathMatch with {} patterns",
              config.path_condition_.path_match.size());
        }
      }

      if (yaml["If"]["PathExclude"]) {
        const auto& exclude_node = yaml["If"]["PathExclude"];
        if (exclude_node.IsScalar()) {
          // Single pattern
          config.path_condition_.path_exclude.push_back(
              exclude_node.as<std::string>());
          config.logger_->debug(
              "Loaded PathExclude: {}", config.path_condition_.path_exclude[0]);
        } else if (exclude_node.IsSequence()) {
          // List of patterns
          for (const auto& pattern : exclude_node) {
            config.path_condition_.path_exclude.push_back(
                pattern.as<std::string>());
          }
          config.logger_->debug(
              "Loaded PathExclude with {} patterns",
              config.path_condition_.path_exclude.size());
        }
      }
    }

    // Parse AutoDiscover section (supports both bool and object formats)
    if (yaml["AutoDiscover"]) {
      const auto& auto_discover_node = yaml["AutoDiscover"];

      if (auto_discover_node.IsScalar()) {
        // Old format: AutoDiscover: true/false
        config.auto_discover_ = auto_discover_node.as<bool>();
        config.logger_->debug("Loaded AutoDiscover: {}", config.auto_discover_);
      } else if (auto_discover_node.IsMap()) {
        // New format: AutoDiscover: {Enabled: true, DiscoverDirs: [...]}
        if (auto_discover_node["Enabled"]) {
          config.auto_discover_ = auto_discover_node["Enabled"].as<bool>();
          config.logger_->debug(
              "Loaded AutoDiscover.Enabled: {}", config.auto_discover_);
        }

        if (auto_discover_node["DiscoverDirs"]) {
          for (const auto& dir : auto_discover_node["DiscoverDirs"]) {
            config.discover_dirs_.push_back(dir.as<std::string>());
          }
          config.logger_->debug(
              "Loaded AutoDiscover.DiscoverDirs with {} paths",
              config.discover_dirs_.size());
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

auto SlangdConfigFile::ShouldIncludeFile(std::string_view relative_path) const
    -> bool {
  // No conditions specified -> include everything
  if (path_condition_.path_match.empty() &&
      path_condition_.path_exclude.empty()) {
    return true;
  }

  // Convert to string for regex matching (relative_path is already normalized)
  std::string path_str(relative_path);

  try {
    // Check PathMatch: if specified, path must match at least ONE pattern (OR)
    if (!path_condition_.path_match.empty()) {
      bool matches_any = false;
      for (const auto& pattern : path_condition_.path_match) {
        std::regex match_pattern(pattern);
        if (std::regex_match(path_str, match_pattern)) {
          matches_any = true;
          break;  // Found a match, no need to check remaining patterns
        }
      }
      if (!matches_any) {
        return false;  // Doesn't match any PathMatch pattern -> exclude
      }
    }

    // Check PathExclude: if specified and matches ANY pattern, exclude (OR)
    if (!path_condition_.path_exclude.empty()) {
      for (const auto& pattern : path_condition_.path_exclude) {
        std::regex exclude_pattern(pattern);
        if (std::regex_match(path_str, exclude_pattern)) {
          return false;  // Matches a PathExclude pattern -> exclude
        }
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

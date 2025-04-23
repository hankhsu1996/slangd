#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

namespace slangd {

// Represents the contents of a .slangd configuration file
class SlangdConfigFile {
 public:
  // File lists from .f files
  struct FileLists {
    std::vector<std::filesystem::path> paths;
    bool absolute = false;
  };

  // Constructor with optional logger
  explicit SlangdConfigFile(std::shared_ptr<spdlog::logger> logger = nullptr);

  // Create a default configuration with sensible fallback values
  static auto CreateDefault(std::shared_ptr<spdlog::logger> logger = nullptr)
      -> SlangdConfigFile;

  // Load a configuration from a .slangd file in the specified root directory
  // Returns std::nullopt if file doesn't exist or has critical parsing errors
  static auto LoadFromFile(
      const std::filesystem::path& root,
      std::shared_ptr<spdlog::logger> logger = nullptr)
      -> std::optional<SlangdConfigFile>;

  // Accessors
  [[nodiscard]] auto GetFileLists() const -> const FileLists& {
    return file_lists_;
  }

  [[nodiscard]] auto GetFiles() const
      -> const std::vector<std::filesystem::path>& {
    return files_;
  }

  [[nodiscard]] auto GetIncludeDirs() const
      -> const std::vector<std::filesystem::path>& {
    return include_dirs_;
  }

  [[nodiscard]] auto GetDefines() const -> const std::vector<std::string>& {
    return defines_;
  }

  // Helper methods
  [[nodiscard]] auto HasFileSources() const -> bool {
    return !file_lists_.paths.empty() || !files_.empty();
  }

  [[nodiscard]] auto HasAnySettings() const -> bool {
    return HasFileSources() || !include_dirs_.empty() || !defines_.empty();
  }

 private:
  // Logger for logging
  std::shared_ptr<spdlog::logger> logger_;

  // List of .f file paths to load
  FileLists file_lists_;

  // Additional individual source files
  std::vector<std::filesystem::path> files_;

  // Include directories for `include files
  std::vector<std::filesystem::path> include_dirs_;

  // Macro definitions (NAME or NAME=value)
  std::vector<std::string> defines_;
};

}  // namespace slangd

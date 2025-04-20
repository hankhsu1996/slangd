#include "slangd/utils/source_utils.hpp"

#include <filesystem>
#include <string>

namespace slangd {

auto ExtractFilename(const std::string& path) -> std::string {
  std::string filename = path;
  size_t last_slash = path.find_last_of("/\\");
  if (last_slash != std::string::npos) {
    filename = path.substr(last_slash + 1);
  }
  return filename;
}

auto IsLocationInDocument(
    const slang::SourceLocation& location,
    const std::shared_ptr<slang::SourceManager>& source_manager,
    const std::string& uri) -> bool {
  // Check if we have a valid location
  if (!location) {
    return false;
  }

  // Get the full path of the source file for this location
  auto full_source_path = std::string(source_manager->getFileName(location));

  // Extract just the filename parts from both the source path and URI
  std::string source_filename = ExtractFilename(full_source_path);
  std::string uri_filename = ExtractFilename(uri);

  // Compare the filenames
  // Note: This is a simple comparison that works for basic cases.
  // In a more robust implementation, you might want to normalize paths
  // and compare them fully, or have a more sophisticated URI matching logic.
  return source_filename == uri_filename;
}

auto IsSystemVerilogFile(const std::string& path) -> bool {
  std::string ext = std::filesystem::path(path).extension().string();
  return ext == ".sv" || ext == ".svh" || ext == ".v" || ext == ".vh";
}

auto IsConfigFile(const std::string& path) -> bool {
  std::string ext = std::filesystem::path(path).extension().string();
  return ext == ".slangd";
}

auto NormalizePath(const std::string& path) -> std::string {
  try {
    return std::filesystem::canonical(path).string();
  } catch (const std::exception& e) {
    // If canonical fails (e.g., file doesn't exist), return the original path
    return path;
  }
}

}  // namespace slangd

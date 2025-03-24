#include "slangd/utils/source_utils.hpp"

#include <string>

namespace slangd {

std::string ExtractFilename(const std::string& path) {
  std::string filename = path;
  size_t last_slash = path.find_last_of("/\\");
  if (last_slash != std::string::npos) {
    filename = path.substr(last_slash + 1);
  }
  return filename;
}

bool IsLocationInDocument(
    const slang::SourceLocation& location,
    const std::shared_ptr<slang::SourceManager>& source_manager,
    const std::string& uri) {
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

}  // namespace slangd

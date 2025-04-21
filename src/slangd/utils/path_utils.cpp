#include "slangd/utils/path_utils.hpp"

#include <filesystem>
#include <regex>

#include <fmt/format.h>

namespace slangd {

auto UriToPath(std::string_view uri) -> std::string {
  if (!uri.starts_with("file://")) {
    return std::string(uri);
  }

  // Strip "file://"
  std::string path(uri.substr(7));

  // Handle Windows: file:///C:/path → C:/path
  if (path.size() >= 3 && path[0] == '/' && path[2] == ':') {
    path = path.substr(1);
  }

  // Replace percent-encoded sequences
  static const std::regex kEscapeRegex("%([0-9A-Fa-f]{2})");

  std::string result;
  std::regex_iterator<std::string::iterator> it(
      path.begin(), path.end(), kEscapeRegex);
  std::regex_iterator<std::string::iterator> end;

  std::size_t last_pos = 0;
  while (it != end) {
    result.append(path, last_pos, it->position() - last_pos);

    std::string hex = (*it)[1];
    try {
      char c = static_cast<char>(std::stoi(hex, nullptr, 16));
      result += c;
    } catch (const std::exception& e) {
      result += it->str();  // fallback to raw
    }

    last_pos = it->position() + it->length();
    ++it;
  }

  result.append(path, last_pos, path.length() - last_pos);
  return result;
}

auto PathToUri(std::string_view path) -> std::string {
  std::string result = "file://";

  if (path.size() >= 2 && path[1] == ':') {
    result += '/';
  }

  for (char c : path) {
    if (c == ' ' || c == '%' || c == '#' || c == '?' ||
        static_cast<unsigned char>(c) > 127 ||
        static_cast<unsigned char>(c) < 32) {
      result += fmt::format("%{:02X}", static_cast<unsigned char>(c));
    } else {
      result += c;
    }
  }

  return result;
}

auto IsFileUri(std::string_view uri) -> bool {
  return uri.substr(0, 7) == "file://";
}

auto NormalizePath(std::string_view path) -> std::string {
  try {
    std::filesystem::path fs_path(path);
    if (std::filesystem::exists(fs_path)) {
      return std::filesystem::canonical(fs_path).string();
    }
  } catch (const std::exception&) {
    // Fall back to original path if we can't canonicalize
  }
  return std::string(path);
}

auto ExtractFilename(std::string_view path) -> std::string {
  std::filesystem::path fs_path(path);
  return fs_path.filename().string();
}

auto IsSystemVerilogFile(std::string_view path) -> bool {
  std::filesystem::path fs_path(path);
  std::string ext = fs_path.extension().string();

  if (ext.empty()) {
    return false;
  }

  // Convert to lowercase for case-insensitive comparison
  std::ranges::transform(
      ext, ext.begin(), [](unsigned char c) { return std::tolower(c); });

  return ext == ".sv" || ext == ".svh" || ext == ".v" || ext == ".vh";
}

auto IsConfigFile(std::string_view path) -> bool {
  std::filesystem::path fs_path(path);
  std::string ext = fs_path.extension().string();

  if (ext.empty()) {
    return false;
  }

  return ext == ".slangd";
}

auto IsLocationInDocument(
    const slang::SourceLocation& location,
    const std::shared_ptr<slang::SourceManager>& source_manager,
    std::string_view uri) -> bool {
  if (!location || !source_manager) {
    return false;
  }

  std::string document_path = UriToPath(uri);
  std::string location_path =
      std::string(source_manager->getFileName(location));

  return NormalizePath(document_path) == NormalizePath(location_path);
}

}  // namespace slangd

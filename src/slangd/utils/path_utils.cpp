#include "slangd/utils/path_utils.hpp"

#include <filesystem>
#include <regex>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

namespace slangd {
using std::filesystem::path;

inline auto HasExtension(
    std::filesystem::path path, std::initializer_list<std::string_view> exts)
    -> bool {
  std::string ext = path.extension().string();
  if (ext.empty()) {
    return false;
  }
  std::ranges::transform(
      ext, ext.begin(), [](unsigned char c) { return std::tolower(c); });
  return std::ranges::find(exts, ext) != exts.end();
}

auto IsSystemVerilogFile(std::filesystem::path path) -> bool {
  return HasExtension(path, {".sv", ".svh", ".v", ".vh"});
}

auto IsConfigFile(std::filesystem::path path) -> bool {
  return path.filename() == ".slangd";
}

auto UriToPath(std::string_view uri) -> std::filesystem::path {
  if (!uri.starts_with("file://")) {
    return {uri};
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
  return NormalizePath(result);
}

auto PathToUri(std::filesystem::path path) -> std::string {
  std::string result = "file://";

  if (path.string().size() >= 2 && path.string()[1] == ':') {
    result += '/';
  }

  for (char c : path.string()) {
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

auto NormalizePath(std::filesystem::path path) -> std::filesystem::path {
  try {
    // Only canonicalize if the file actually exists
    // For synthetic/test files, just return the path as-is
    if (std::filesystem::exists(path)) {
      return std::filesystem::canonical(path);
    }
    return path;
  } catch (const std::exception&) {
    return path;
  }
}

auto NormalizeUri(std::string_view uri) -> std::string {
  if (!uri.starts_with("file://")) {
    return std::string(uri);
  }

  try {
    auto path = std::filesystem::path(uri.substr(7));  // Remove "file://"
    auto canonical = std::filesystem::weakly_canonical(path);
    return "file://" + canonical.string();
  } catch (...) {
    return std::string(uri);  // Return original if normalization fails
  }
}

auto IsLocationInDocument(
    const slang::SourceLocation& location,
    const slang::SourceManager& source_manager, std::string_view uri) -> bool {
  if (!location) {
    return false;
  }

  std::string document_path = UriToPath(uri);
  std::filesystem::path location_path =
      source_manager.getFullPath(location.buffer());

  return NormalizePath(document_path) == NormalizePath(location_path);
}

}  // namespace slangd

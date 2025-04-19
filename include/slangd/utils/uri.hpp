#pragma once

#include <regex>
#include <string>
#include <string_view>

namespace slangd {

// Convert URI to local file path
// Examples:
// "file:///home/user/file.sv" -> "/home/user/file.sv"
// "file:///c:/Users/user/file.sv" -> "c:/Users/user/file.sv"
inline auto UriToPath(std::string_view uri) -> std::string {
  if (uri.substr(0, 7) != "file://") {
    return std::string(uri);
  }

  // Remove the file:// prefix
  std::string path(uri.substr(7));

  // Handle Windows paths
  if (path.size() >= 3 && path[0] == '/' && path[2] == ':') {
    // Remove the leading slash for Windows drives
    // "/c:/path" -> "c:/path"
    path = path.substr(1);
  }

  // Replace URL encoded characters
  static const std::regex kEscapeRegex("%([0-9A-Fa-f]{2})");
  std::string result;

  std::regex_iterator<std::string::iterator> it(
      path.begin(), path.end(), kEscapeRegex);
  std::regex_iterator<std::string::iterator> end;

  std::size_t last_pos = 0;
  while (it != end) {
    result.append(path, last_pos, it->position() - last_pos);

    // Convert hex code to character
    std::string hex = it->str().substr(1);
    try {
      char c = static_cast<char>(std::stoi(hex, nullptr, 16));
      result += c;
    } catch (...) {
      result += it->str();
    }

    last_pos = it->position() + it->length();
    ++it;
  }

  result.append(path, last_pos, path.length() - last_pos);
  return result;
}

// Convert local file path to URI
// Examples:
// "/home/user/file.sv" -> "file:///home/user/file.sv"
// "c:/Users/user/file.sv" -> "file:///c:/Users/user/file.sv"
inline auto PathToUri(std::string_view path) -> std::string {
  std::string result = "file://";

  // For Windows paths add an extra slash before the drive letter
  if (path.size() >= 2 && path[1] == ':') {
    result += '/';
  }

  // Encode special characters
  for (char c : path) {
    if (c == ' ' || c == '%' || c == '#' || c == '?' ||
        static_cast<unsigned char>(c) > 127 ||
        static_cast<unsigned char>(c) < 32) {
      char hex[4];
      std::snprintf(hex, sizeof(hex), "%%%02X", static_cast<unsigned char>(c));
      result += hex;
    } else {
      result += c;
    }
  }

  return result;
}

// Check if the URI starts with file://
inline auto IsFileUri(std::string_view uri) -> bool {
  return uri.substr(0, 7) == "file://";
}

}  // namespace slangd

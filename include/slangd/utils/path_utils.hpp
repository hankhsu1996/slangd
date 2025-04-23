#pragma once

#include <memory>
#include <string>
#include <string_view>

#include <slang/text/SourceLocation.h>
#include <slang/text/SourceManager.h>

namespace slangd {

// URI operations
[[nodiscard]] auto UriToPath(std::string_view uri) -> std::filesystem::path;
[[nodiscard]] auto PathToUri(std::filesystem::path path) -> std::string;
[[nodiscard]] auto IsFileUri(std::string_view uri) -> bool;

// Path operations
[[nodiscard]] auto NormalizePath(std::filesystem::path path)
    -> std::filesystem::path;
[[nodiscard]] auto IsSystemVerilogFile(std::filesystem::path path) -> bool;
[[nodiscard]] auto IsConfigFile(std::filesystem::path path) -> bool;

// Source location operations
[[nodiscard]] auto IsLocationInDocument(
    const slang::SourceLocation& location,
    const std::shared_ptr<slang::SourceManager>& source_manager,
    std::string_view uri) -> bool;

// Combined operations
[[nodiscard]] inline auto UriToNormalizedPath(std::string_view uri)
    -> std::filesystem::path {
  return NormalizePath(UriToPath(uri));
}

[[nodiscard]] inline auto IsSubPath(
    const std::filesystem::path& parent, const std::filesystem::path& child)
    -> bool {
  return std::mismatch(parent.begin(), parent.end(), child.begin()).first ==
         parent.end();
}

}  // namespace slangd

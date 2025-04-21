#pragma once

#include <memory>
#include <string>
#include <string_view>

#include <slang/text/SourceLocation.h>
#include <slang/text/SourceManager.h>

namespace slangd {

// URI operations
[[nodiscard]] auto UriToPath(std::string_view uri) -> std::string;
[[nodiscard]] auto PathToUri(std::string_view path) -> std::string;
[[nodiscard]] auto IsFileUri(std::string_view uri) -> bool;

// Path operations
[[nodiscard]] auto NormalizePath(std::string_view path) -> std::string;
[[nodiscard]] auto ExtractFilename(std::string_view path) -> std::string;
[[nodiscard]] auto IsSystemVerilogFile(std::string_view path) -> bool;
[[nodiscard]] auto IsConfigFile(std::string_view path) -> bool;

// Source location operations
[[nodiscard]] auto IsLocationInDocument(
    const slang::SourceLocation& location,
    const std::shared_ptr<slang::SourceManager>& source_manager,
    std::string_view uri) -> bool;

// Combined operations
[[nodiscard]] inline auto UriToNormalizedPath(std::string_view uri)
    -> std::string {
  return NormalizePath(UriToPath(uri));
}

}  // namespace slangd

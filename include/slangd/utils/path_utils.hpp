#pragma once

#include <memory>
#include <string>
#include <string_view>

#include <slang/text/SourceLocation.h>
#include <slang/text/SourceManager.h>

namespace slangd {

// File type checks
[[nodiscard]] auto IsSystemVerilogFile(std::filesystem::path path) -> bool;
[[nodiscard]] auto IsConfigFile(std::filesystem::path path) -> bool;

// URI operations
[[nodiscard]] auto UriToPath(std::string_view uri) -> std::filesystem::path;
[[nodiscard]] auto PathToUri(std::filesystem::path path) -> std::string;
[[nodiscard]] auto NormalizePath(std::filesystem::path path)
    -> std::filesystem::path;

// Source location operations
// TODO(hankhsu1996) find a better place for this
[[nodiscard]] auto IsLocationInDocument(
    const slang::SourceLocation& location,
    const std::shared_ptr<slang::SourceManager>& source_manager,
    std::string_view uri) -> bool;

}  // namespace slangd

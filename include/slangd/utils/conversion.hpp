#pragma once

#include <memory>

#include <slang/text/SourceLocation.h>
#include <slang/text/SourceManager.h>

#include "lsp/basic.hpp"

namespace slangd {

/**
 * Converts a Slang source location to an LSP range
 */
lsp::Range ConvertSlangLocationToLspRange(
    const slang::SourceLocation& location,
    const std::shared_ptr<slang::SourceManager>& source_manager);

/**
 * Converts a Slang source ranges to an LSP range
 */
lsp::Range ConvertSlangRangesToLspRange(
    const std::vector<slang::SourceRange>& ranges,
    const std::shared_ptr<slang::SourceManager>& source_manager);

// convert const std::optional<nlohmann::json>& json to LSP strong type
template <typename T>
T FromJson(const std::optional<nlohmann::json>& json) {
  return json.value().get<T>();
}

}  // namespace slangd

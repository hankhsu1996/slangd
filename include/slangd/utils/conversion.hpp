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

}  // namespace slangd

#pragma once

#include <slang/ast/Symbol.h>
#include <slang/text/SourceLocation.h>
#include <slang/text/SourceManager.h>

#include "lsp/basic.hpp"

namespace slangd {

// Convert a Slang source location to a zero-length LSP range at that point
auto ConvertSlangLocationToLspRange(
    const slang::SourceLocation& location,
    const slang::SourceManager& source_manager) -> lsp::Range;

// Convert a Slang source range to an LSP range
auto ConvertSlangRangeToLspRange(
    const slang::SourceRange& range, const slang::SourceManager& source_manager)
    -> lsp::Range;

// Convert an LSP position to a Slang source location
auto ConvertLspPositionToSlangLocation(
    const lsp::Position& position, const slang::BufferID& buffer_id,
    const slang::SourceManager& source_manager) -> slang::SourceLocation;

// Convert a Slang source location to an LSP location (with URI and a
// zero-length range)
auto ConvertSlangLocationToLspLocation(
    const slang::SourceLocation& location,
    const slang::SourceManager& source_manager) -> lsp::Location;

// Convert a Slang source location to an LSP position
auto ConvertSlangLocationToLspPosition(
    const slang::SourceLocation& location,
    const slang::SourceManager& source_manager) -> lsp::Position;

// Create LSP range for a symbol's name using its location and name length.
// Returns nullopt if symbol location is invalid.
inline auto CreateSymbolLspRange(
    const slang::ast::Symbol& symbol,
    const slang::SourceManager& source_manager) -> std::optional<lsp::Range> {
  if (!symbol.location.valid()) {
    return std::nullopt;
  }

  // Convert to LSP position and extend by name length
  lsp::Position start =
      ConvertSlangLocationToLspPosition(symbol.location, source_manager);
  lsp::Position end = start;
  end.character += static_cast<int>(symbol.name.length());

  return lsp::Range{.start = start, .end = end};
}

// Create LSP location (range + URI) for a symbol's name.
// Returns nullopt if symbol location is invalid.
inline auto CreateSymbolLspLocation(
    const slang::ast::Symbol& symbol,
    const slang::SourceManager& source_manager)
    -> std::optional<lsp::Location> {
  auto range = CreateSymbolLspRange(symbol, source_manager);
  if (!range) {
    return std::nullopt;
  }

  // Get URI from symbol location
  lsp::Location location =
      ConvertSlangLocationToLspLocation(symbol.location, source_manager);
  location.range = *range;

  return location;
}

// convert const std::optional<nlohmann::json>& json to LSP strong type
template <typename T>
auto FromJson(const std::optional<nlohmann::json>& json) -> T {
  return json.value().get<T>();
}

}  // namespace slangd

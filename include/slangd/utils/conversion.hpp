#pragma once

#include <memory>

#include <slang/ast/Compilation.h>
#include <slang/ast/Expression.h>
#include <slang/ast/Scope.h>
#include <slang/ast/Symbol.h>
#include <slang/text/SourceLocation.h>
#include <slang/text/SourceManager.h>
#include <slang/util/Enum.h>
#include <spdlog/spdlog.h>

#include "lsp/basic.hpp"

namespace slangd {

// Convert Slang source location to zero-length LSP range at that point
auto ToLspRange(
    const slang::SourceLocation& location,
    const slang::SourceManager& source_manager) -> lsp::Range;

// Convert Slang source range to LSP range
auto ToLspRange(
    const slang::SourceRange& range, const slang::SourceManager& source_manager)
    -> lsp::Range;

// Convert LSP position to Slang source location
auto ToSlangLocation(
    const lsp::Position& position, const slang::BufferID& buffer_id,
    const slang::SourceManager& source_manager) -> slang::SourceLocation;

// Convert Slang source location to LSP location (URI + zero-length range)
auto ToLspLocation(
    const slang::SourceLocation& location,
    const slang::SourceManager& source_manager) -> lsp::Location;

// Convert Slang source location to LSP position
auto ToLspPosition(
    const slang::SourceLocation& location,
    const slang::SourceManager& source_manager) -> lsp::Position;

// Create LSP range for a symbol's name using an explicit SourceManager.
// Returns a range that spans from the symbol location to location + name
// length. Returns nullopt if symbol has invalid location or negative line
// numbers. This is the low-level function - use CreateSymbolRange() for
// automatic SM derivation.
inline auto CreateSymbolRangeWithSM(
    const slang::ast::Symbol& symbol,
    const slang::SourceManager& source_manager) -> std::optional<lsp::Range> {
  // Check valid location
  if (!symbol.location.valid()) {
    return std::nullopt;
  }

  // Compute range: symbol name location + length
  lsp::Position start = ToLspPosition(symbol.location, source_manager);

  // Filter out built-in symbols with invalid coordinates (line == -1)
  // These are added automatically by Slang (e.g., class randomize() methods)
  if (start.line < 0) {
    return std::nullopt;
  }

  lsp::Position end = start;
  end.character += static_cast<int>(symbol.name.length());

  return lsp::Range{.start = start, .end = end};
}

// Create LSP location (URI + range) for a symbol using an explicit
// SourceManager.
//
// LOW-LEVEL FUNCTION: This is an implementation detail. Most code should use
// CreateSymbolLocation() instead, which automatically gets the correct
// SourceManager from symbol.getCompilation().
//
// WARNING: Manually passing a SourceManager that doesn't match the symbol's
// compilation will cause BufferID mismatches and invalid coordinates. Only use
// this if you're absolutely certain the SourceManager is correct for the
// symbol's location.
inline auto CreateSymbolLocationWithSM(
    const slang::ast::Symbol& symbol,
    const slang::SourceManager& source_manager)
    -> std::optional<lsp::Location> {
  // Compute the range (validates location, checks for negative lines)
  auto range_opt = CreateSymbolRangeWithSM(symbol, source_manager);
  if (!range_opt.has_value()) {
    return std::nullopt;
  }

  // Get base location for URI extraction
  lsp::Location location = ToLspLocation(symbol.location, source_manager);
  location.range = *range_opt;

  return location;
}

// Create LSP range for a symbol's name.
// Automatically derives SourceManager from the symbol's compilation.
// SAFE CONVERSION: Prevents BufferID mismatch when symbol is from preamble.
// Returns nullopt if symbol has no source manager or invalid location.
inline auto CreateSymbolRange(const slang::ast::Symbol& symbol)
    -> std::optional<lsp::Range> {
  const auto& compilation = symbol.getCompilation();
  const auto* source_manager = compilation.getSourceManager();
  if (source_manager == nullptr) {
    return std::nullopt;
  }

  return CreateSymbolRangeWithSM(symbol, *source_manager);
}

// Create LSP location (URI + range) for a symbol's name.
// Automatically derives SourceManager from the symbol's compilation.
// SAFE CONVERSION: Prevents BufferID mismatch when symbol is from preamble.
// Returns nullopt if symbol has no source manager or invalid location.
inline auto CreateSymbolLocation(
    const slang::ast::Symbol& symbol, std::shared_ptr<spdlog::logger> logger)
    -> std::optional<lsp::Location> {
  // Trace before dangerous operations (crash investigation)
  logger->trace(
      "CreateSymbolLocation: name='{}' kind={}", symbol.name,
      toString(symbol.kind));

  // Use symbol's compilation to get the correct SourceManager
  // This handles both cross-compilation cases:
  // 1. Interface fields: symbol has preamble compilation (via Slang fix)
  // 2. Specialized classes: symbol has overlay compilation where instantiated
  const auto& compilation = symbol.getCompilation();
  const auto* source_manager = compilation.getSourceManager();
  if (source_manager == nullptr) {
    return std::nullopt;
  }

  return CreateSymbolLocationWithSM(symbol, *source_manager);
}

// Create LSP location from Slang range, using symbol's SourceManager.
// SAFE CONVERSION: Automatically derives correct SourceManager from symbol's
// compilation, preventing BufferID mismatch crashes.
//
// Use this for ANY range conversion where the range might belong to a different
// compilation than the current session (e.g., preamble symbols).
//
// Returns nullopt if compilation has no SourceManager or range is invalid.
inline auto CreateLspLocation(
    const slang::ast::Symbol& symbol, slang::SourceRange range,
    std::shared_ptr<spdlog::logger> logger) -> std::optional<lsp::Location> {
  // Trace before dangerous operations (crash investigation)
  logger->trace(
      "CreateLspLocation(symbol): name='{}' kind={}", symbol.name,
      toString(symbol.kind));

  // Use symbol's compilation to get the correct SourceManager
  // This handles both cross-compilation cases correctly
  const auto& compilation = symbol.getCompilation();
  const auto* sm = compilation.getSourceManager();
  if (sm == nullptr || !range.start().valid()) {
    return std::nullopt;
  }

  // Explicit validation: Check if location's BufferID exists in this SM
  // This happens with cross-compilation (preamble symbols in overlay SM)
  if (!sm->isValidLocation(range.start())) {
    return std::nullopt;
  }

  // Get location (with URI) and update its range
  lsp::Location location = ToLspLocation(range.start(), *sm);
  location.range = ToLspRange(range, *sm);

  // Defensive check: Ensure conversion produced valid coordinates
  // Both line and character should be >= 0
  if (location.range.start.line < 0 || location.range.start.character < 0) {
    return std::nullopt;
  }

  return location;
}

// Create LSP location from arbitrary range, using Expression's SourceManager.
// SAFE CONVERSION: Automatically derives SourceManager from expression's
// compilation, preventing BufferID mismatch crashes.
//
// This is useful for expression-related ranges (e.g., member access ranges,
// call ranges) that need conversion using the overlay compilation's SM.
//
// Returns nullopt if:
// - Expression has no compilation context
// - Compilation has no SourceManager
// - Range is invalid
inline auto CreateLspLocation(
    const slang::ast::Expression& expr, slang::SourceRange range,
    std::shared_ptr<spdlog::logger> logger) -> std::optional<lsp::Location> {
  // Trace before dangerous operations (crash investigation)
  logger->trace("CreateLspLocation(expr): kind={}", toString(expr.kind));

  // Get the compilation from expression directly
  if (expr.compilation == nullptr) {
    return std::nullopt;
  }

  const auto* sm = expr.compilation->getSourceManager();
  if (sm == nullptr || !range.start().valid()) {
    return std::nullopt;
  }

  // Explicit validation: Check if location's BufferID exists in this SM
  if (!sm->isValidLocation(range.start())) {
    return std::nullopt;
  }

  // Get location (with URI) and update its range
  lsp::Location location = ToLspLocation(range.start(), *sm);
  location.range = ToLspRange(range, *sm);

  // Defensive check: Ensure conversion produced valid coordinates
  if (location.range.start.line < 0 || location.range.start.character < 0) {
    return std::nullopt;
  }

  return location;
}

// convert const std::optional<nlohmann::json>& json to LSP strong type
template <typename T>
auto FromJson(const std::optional<nlohmann::json>& json) -> T {
  return json.value().get<T>();
}

}  // namespace slangd

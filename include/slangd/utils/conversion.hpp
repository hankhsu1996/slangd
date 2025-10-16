#pragma once

#include <slang/ast/Compilation.h>
#include <slang/ast/Scope.h>
#include <slang/ast/Symbol.h>
#include <slang/text/SourceLocation.h>
#include <slang/text/SourceManager.h>

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

// Create LSP location (URI + range) for a symbol's name.
// Automatically uses the correct SourceManager from the symbol's compilation.
// This prevents BufferID mismatch crashes by ensuring preamble symbols use
// preamble's SM and overlay symbols use overlay's SM.
// Returns nullopt if symbol has no parent scope, no source manager, or invalid
// location.
inline auto CreateSymbolLspLocation(const slang::ast::Symbol& symbol)
    -> std::optional<lsp::Location> {
  // Derive SourceManager from symbol's own compilation
  const auto* scope = symbol.getParentScope();
  if (scope == nullptr) {
    return std::nullopt;
  }

  const auto& compilation = scope->getCompilation();
  const auto* source_manager = compilation.getSourceManager();
  if (source_manager == nullptr) {
    return std::nullopt;
  }

  // Check valid location
  if (!symbol.location.valid()) {
    return std::nullopt;
  }

  // Compute range: symbol name location + length
  lsp::Position start = ToLspPosition(symbol.location, *source_manager);

  // Filter out built-in symbols with invalid coordinates (line == -1)
  // These are added automatically by Slang (e.g., class randomize() methods)
  if (start.line < 0) {
    return std::nullopt;
  }

  lsp::Position end = start;
  end.character += static_cast<int>(symbol.name.length());

  // Get base location (for URI extraction) and set our computed range
  lsp::Location location = ToLspLocation(symbol.location, *source_manager);
  location.range = {.start = start, .end = end};

  return location;
}

// Create LSP location from Slang range, using symbol's SourceManager.
// Use this when symbol.location doesn't point to the name (e.g., GenerateBlock,
// DefinitionSymbol where symbol.location points to keyword, not name).
// Derives SM safely from symbol while using explicit range for accuracy.
inline auto CreateLspLocation(
    const slang::ast::Symbol& symbol, slang::SourceRange range)
    -> std::optional<lsp::Location> {
  // Derive SourceManager from symbol's compilation (safe!)
  const auto* scope = symbol.getParentScope();
  if (scope == nullptr) {
    return std::nullopt;
  }

  const auto& compilation = scope->getCompilation();
  const auto* sm = compilation.getSourceManager();
  if (sm == nullptr || !range.start().valid()) {
    return std::nullopt;
  }

  // Convert Slang range to LSP location
  auto lsp_range = ToLspRange(range, *sm);
  auto uri = ToLspLocation(range.start(), *sm).uri;
  return lsp::Location{.uri = uri, .range = lsp_range};
}

// convert const std::optional<nlohmann::json>& json to LSP strong type
template <typename T>
auto FromJson(const std::optional<nlohmann::json>& json) -> T {
  return json.value().get<T>();
}

}  // namespace slangd

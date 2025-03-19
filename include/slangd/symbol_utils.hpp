#pragma once

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "lsp/document_symbol.hpp"
#include "slang/ast/Compilation.h"
#include "slang/ast/Scope.h"
#include "slang/ast/Symbol.h"
#include "slang/text/SourceLocation.h"
#include "slang/text/SourceManager.h"

namespace slangd {

/**
 * Helper function to unwrap TransparentMember symbols
 *
 * @param symbol The symbol to unwrap
 * @return The unwrapped symbol
 */
const slang::ast::Symbol& GetUnwrappedSymbol(const slang::ast::Symbol& symbol);

/**
 * Maps a Slang symbol to an LSP symbol kind
 *
 * @param symbol The symbol to map (must be already unwrapped)
 * @return The corresponding LSP symbol kind
 */
lsp::SymbolKind MapSymbolToLspSymbolKind(const slang::ast::Symbol& symbol);

/**
 * Converts a Slang source location to an LSP range
 */
lsp::Range ConvertSlangLocationToLspRange(
    const slang::SourceLocation& location,
    const std::shared_ptr<slang::SourceManager>& source_manager);

/**
 * Gets a range that covers just the symbol's name
 */
lsp::Range GetSymbolNameLocationRange(
    const slang::ast::Symbol& symbol,
    const std::shared_ptr<slang::SourceManager>& source_manager);

/**
 * Determines if a symbol should be included in document symbols
 */
bool ShouldIncludeSymbol(
    const slang::ast::Symbol& symbol,
    const std::shared_ptr<slang::SourceManager>& source_manager,
    const std::string& uri);

/**
 * Process members of a scope
 *
 * This function handles unwrapping of scope members before processing.
 */
void ProcessScopeMembers(
    const slang::ast::Scope& scope, lsp::DocumentSymbol& parent_symbol,
    const std::shared_ptr<slang::SourceManager>& source_manager,
    const std::string& uri);

/**
 * Recursively builds a hierarchy of document symbols
 *
 * @param symbol The symbol to process (must be already unwrapped)
 */
void BuildDocumentSymbolHierarchy(
    const slang::ast::Symbol& symbol,
    std::vector<lsp::DocumentSymbol>& document_symbols,
    const std::shared_ptr<slang::SourceManager>& source_manager,
    const std::string& uri, std::unordered_set<std::string>& seen_names);

/**
 * Gets document symbols for a compilation
 *
 * This is an entry point function that handles unwrapping symbols
 * before passing them to internal functions.
 */
std::vector<lsp::DocumentSymbol> GetDocumentSymbols(
    slang::ast::Compilation& compilation,
    const std::shared_ptr<slang::SourceManager>& source_manager,
    const std::string& uri);

}  // namespace slangd

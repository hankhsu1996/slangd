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
 * Maps a Slang symbol to an LSP symbol kind.
 *
 * @param symbol The Slang symbol to map
 * @return The corresponding LSP symbol kind
 */
lsp::SymbolKind MapSymbolToLspSymbolKind(const slang::ast::Symbol& symbol);

/**
 * Converts a Slang source location to an LSP range.
 *
 * @param location The Slang source location
 * @param source_manager The source manager to use for location information
 * @return The corresponding LSP range
 */
lsp::Range ConvertSlangLocationToLspRange(
    const slang::SourceLocation& location,
    const std::shared_ptr<slang::SourceManager>& source_manager);

/**
 * Gets the selection range for a symbol (typically just the name).
 *
 * @param symbol The symbol to get the selection range for
 * @param source_manager The source manager to use for location information
 * @return The LSP range corresponding to the symbol name
 */
lsp::Range GetSymbolNameLocationRange(
    const slang::ast::Symbol& symbol,
    const std::shared_ptr<slang::SourceManager>& source_manager);

/**
 * Determines if a symbol should be included in the document symbols for a given
 * URI.
 *
 * @param symbol The symbol to check
 * @param source_manager The source manager to use for location information
 * @param uri The URI of the current document
 * @return true if the symbol should be included, false otherwise
 */
bool ShouldIncludeSymbol(
    const slang::ast::Symbol& symbol,
    const std::shared_ptr<slang::SourceManager>& source_manager,
    const std::string& uri);

/**
 * Processes the members of a scope and adds them as children to the parent
 * symbol.
 *
 * @param scope The scope to process
 * @param parent_symbol The parent symbol to add children to
 * @param source_manager The source manager to use for location information
 * @param uri The URI of the current document
 */
void ProcessScopeMembers(
    const slang::ast::Scope& scope, lsp::DocumentSymbol& parent_symbol,
    const std::shared_ptr<slang::SourceManager>& source_manager,
    const std::string& uri);

/**
 * Recursively builds a document symbol hierarchy for a given symbol.
 *
 * @param symbol The root symbol to start building from
 * @param document_symbols Output vector to append document symbols to
 * @param source_manager The source manager to use for location information
 * @param uri The URI of the current document
 * @param seen_names Set of symbol names already processed in the current scope
 */
void BuildDocumentSymbolHierarchy(
    const slang::ast::Symbol& symbol,
    std::vector<lsp::DocumentSymbol>& document_symbols,
    const std::shared_ptr<slang::SourceManager>& source_manager,
    const std::string& uri, std::unordered_set<std::string>& seen_names);

/**
 * Gets all document symbols for a given compilation and URI.
 *
 * @param compilation The compilation containing the symbols
 * @param source_manager The source manager to use for location information
 * @param uri The URI of the current document
 * @return A vector of document symbols representing the hierarchy
 */
std::vector<lsp::DocumentSymbol> GetDocumentSymbols(
    slang::ast::Compilation& compilation,
    const std::shared_ptr<slang::SourceManager>& source_manager,
    const std::string& uri);

}  // namespace slangd

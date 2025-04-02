#pragma once

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include <slang/ast/Compilation.h>
#include <slang/ast/Scope.h>
#include <slang/ast/Symbol.h>
#include <slang/text/SourceManager.h>

#include "lsp/basic.hpp"
#include "lsp/document_features.hpp"

namespace slangd {

/**
 * Helper function to unwrap TransparentMember symbols
 *
 * @param symbol The symbol to unwrap
 * @return The unwrapped symbol
 */
const slang::ast::Symbol& GetUnwrappedSymbol(const slang::ast::Symbol& symbol);

/**
 * Determines if a symbol should be included in document symbols
 *
 * This checks several criteria:
 * 1. The symbol has a valid location
 * 2. The symbol has a name
 * 3. The symbol is from the current document
 * 4. The symbol is of a relevant kind (module, package, function, etc.)
 *
 * @param symbol The symbol to evaluate
 * @param source_manager The source manager for location information
 * @param uri The URI of the document being processed
 * @return True if the symbol should be included
 */
bool ShouldIncludeSymbol(
    const slang::ast::Symbol& symbol,
    const std::shared_ptr<slang::SourceManager>& source_manager,
    const std::string& uri);

/**
 * Process the children of a symbol with type-specific traversal logic
 *
 * This function determines both whether to traverse a symbol's children
 * and how to traverse them based on the symbol type
 *
 * @param symbol The symbol whose children should be processed
 * @param parent_symbol The document symbol to which children will be added
 * @param source_manager The source manager for location information
 * @param uri The URI of the document being processed
 */
void ProcessSymbolChildren(
    const slang::ast::Symbol& symbol, lsp::DocumentSymbol& parent_symbol,
    const std::shared_ptr<slang::SourceManager>& source_manager,
    const std::string& uri, slang::ast::Compilation& compilation);

/**
 * Maps a Slang symbol to an LSP symbol kind
 *
 * @param symbol The symbol to map (must be already unwrapped)
 * @return The corresponding LSP symbol kind
 */
lsp::SymbolKind MapSymbolToLspSymbolKind(const slang::ast::Symbol& symbol);

/**
 * Gets a range that covers just the symbol's name
 */
lsp::Range GetSymbolNameLocationRange(
    const slang::ast::Symbol& symbol,
    const std::shared_ptr<slang::SourceManager>& source_manager);

/**
 * Process members of a scope
 *
 * This function handles unwrapping of scope members before processing.
 */
void ProcessScopeMembers(
    const slang::ast::Scope& scope, lsp::DocumentSymbol& parent_symbol,
    const std::shared_ptr<slang::SourceManager>& source_manager,
    const std::string& uri, slang::ast::Compilation& compilation);

/**
 * Recursively builds a hierarchy of document symbols
 *
 * @param symbol The symbol to process (must be already unwrapped)
 */
void BuildDocumentSymbolHierarchy(
    const slang::ast::Symbol& symbol,
    std::vector<lsp::DocumentSymbol>& document_symbols,
    const std::shared_ptr<slang::SourceManager>& source_manager,
    const std::string& uri, std::unordered_set<std::string>& seen_names,
    slang::ast::Compilation& compilation);

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

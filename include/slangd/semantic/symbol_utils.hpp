#pragma once

#include <slang/ast/Symbol.h>
#include <slang/text/SourceLocation.h>
#include <slang/text/SourceManager.h>

#include "lsp/basic.hpp"

namespace slangd::semantic {

// Convert Slang symbol location to LSP range
auto ComputeLspRange(
    const slang::ast::Symbol& symbol,
    const slang::SourceManager& source_manager) -> lsp::Range;

// Check if symbol should be indexed for semantic analysis
auto ShouldIndexForSemanticIndex(const slang::ast::Symbol& symbol) -> bool;

// Check if symbol should be indexed for document symbols
auto ShouldIndexForDocumentSymbols(const slang::ast::Symbol& symbol) -> bool;

// Convert Slang symbol to LSP symbol kind (comprehensive analysis)
auto ConvertToLspKind(const slang::ast::Symbol& symbol) -> lsp::SymbolKind;

// Convert Slang symbol to LSP symbol kind (simplified for document symbols)
auto ConvertToLspKindForDocuments(const slang::ast::Symbol& symbol)
    -> lsp::SymbolKind;

// Unwrap TransparentMemberSymbol to get the actual symbol
auto UnwrapSymbol(const slang::ast::Symbol& symbol)
    -> const slang::ast::Symbol&;

}  // namespace slangd::semantic

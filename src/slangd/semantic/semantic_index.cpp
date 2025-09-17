#include "slangd/semantic/semantic_index.hpp"

#include <slang/ast/Compilation.h>

#include "slangd/semantic/semantic_index_visitor.hpp"

namespace slangd::semantic {

auto SemanticIndex::FromCompilation(slang::ast::Compilation& compilation)
    -> std::unique_ptr<SemanticIndex> {
  auto index = std::unique_ptr<SemanticIndex>(new SemanticIndex());

  // Create visitor for LSP semantic indexing
  auto visitor = SemanticIndexVisitor([&](const slang::ast::Symbol& symbol) {
    SymbolInfo info{.symbol = &symbol, .location = symbol.location};
    index->symbols_[symbol.location] = info;
  });

  // Traverse from root to get all symbols in overlay session
  compilation.getRoot().visit(visitor);

  return index;
}

}  // namespace slangd::semantic

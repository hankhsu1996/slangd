// symbol_index.cpp

#include "slangd/semantic/symbol_index.hpp"

#include <cassert>

#include <spdlog/spdlog.h>

#include "slang/ast/ASTVisitor.h"
#include "slang/ast/expressions/MiscExpressions.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"

namespace slangd::semantic {

auto SymbolIndex::FromCompilation(slang::ast::Compilation& compilation)
    -> SymbolIndex {
  SymbolIndex index;
  auto visitor = slang::ast::makeVisitor(
      // For correct instance body visitation
      [&](auto& self, const slang::ast::InstanceBodySymbol& symbol) {
        self.visitDefault(symbol);
      },
      // For instance definition collection
      [&](auto& self, const slang::ast::InstanceSymbol& symbol) {
        spdlog::debug("SymbolIndex visiting instance symbol {}", symbol.name);
        self.visitDefault(symbol);
      },
      // For variable definition collection
      [&](auto& self, const slang::ast::VariableSymbol& symbol) {
        spdlog::debug("SymbolIndex visiting variable symbol {}", symbol.name);

        const auto& loc = symbol.location;
        SymbolKey key{.bufferId = loc.buffer().getId(), .offset = loc.offset()};
        index.definition_locations_[key] = loc;

        self.visitDefault(symbol);
      },
      // For variable reference collection
      [&](auto& self, const slang::ast::NamedValueExpression& expr) {
        spdlog::debug(
            "SymbolIndex visiting named value expression {}", expr.symbol.name);

        const auto& loc = expr.symbol.location;
        const auto& range = expr.sourceRange;

        SymbolKey key{.bufferId = loc.buffer().getId(), .offset = loc.offset()};
        index.reference_map_[range] = key;

        self.visitDefault(expr);
      });

  compilation.getRoot().visit(visitor);

  return index;
}

auto SymbolIndex::LookupSymbolAt(slang::SourceLocation loc) const
    -> std::optional<SymbolKey> {
  for (const auto& [range, key] : reference_map_) {
    if (range.contains(loc)) {
      return key;
    }
  }
  return std::nullopt;
}

auto SymbolIndex::GetDefinitionLocation(const SymbolKey& key) const
    -> std::optional<slang::SourceLocation> {
  auto it = definition_locations_.find(key);
  if (it != definition_locations_.end()) {
    return it->second;
  }
  return std::nullopt;
}

}  // namespace slangd::semantic

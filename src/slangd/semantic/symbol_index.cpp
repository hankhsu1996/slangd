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
      // Special handling for instance body
      [&](auto& self, const slang::ast::InstanceBodySymbol& symbol) {
        self.visitDefault(symbol);
      },

      // Module/instance definition visitor
      [&](auto& self, const slang::ast::InstanceSymbol& symbol) {
        spdlog::debug("SymbolIndex visiting instance symbol {}", symbol.name);

        const auto& loc = symbol.location;
        if (!loc) {
          self.visitDefault(symbol);
          return;
        }

        SymbolKey key{.bufferId = loc.buffer().getId(), .offset = loc.offset()};

        // Create a range using the symbol name length
        size_t name_length = symbol.name.length();
        auto end_loc =
            slang::SourceLocation(loc.buffer(), loc.offset() + name_length);
        slang::SourceRange symbol_range(loc, end_loc);

        // Store the definition location
        index.definition_locations_[key] = symbol_range;

        // Add definition itself as a reference (for self-reference cases)
        index.reference_map_[symbol_range] = key;

        self.visitDefault(symbol);
      },

      // Variable definition visitor
      [&](auto& self, const slang::ast::VariableSymbol& symbol) {
        spdlog::debug("SymbolIndex visiting variable symbol {}", symbol.name);

        const auto& loc = symbol.location;
        SymbolKey key{.bufferId = loc.buffer().getId(), .offset = loc.offset()};

        // Create a range using the symbol name length
        size_t name_length = symbol.name.length();
        auto end_loc =
            slang::SourceLocation(loc.buffer(), loc.offset() + name_length);
        slang::SourceRange symbol_range(loc, end_loc);

        // Store the definition location
        index.definition_locations_[key] = symbol_range;

        // Add definition itself as a reference (for self-reference cases)
        index.reference_map_[symbol_range] = key;

        self.visitDefault(symbol);
      },

      // Named value reference visitor
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

auto SymbolIndex::GetDefinitionRange(const SymbolKey& key) const
    -> std::optional<slang::SourceRange> {
  auto it = definition_locations_.find(key);
  if (it != definition_locations_.end()) {
    return it->second;
  }
  return std::nullopt;
}

}  // namespace slangd::semantic

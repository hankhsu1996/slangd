// symbol_index.cpp

#include "slangd/semantic/symbol_index.hpp"

#include <cassert>

#include <spdlog/spdlog.h>

#include "slang/ast/ASTVisitor.h"
#include "slang/ast/expressions/MiscExpressions.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/syntax/AllSyntax.h"

namespace slangd::semantic {

void SymbolIndex::AddDefinition(
    const SymbolKey& key, const slang::SourceRange& range) {
  // Store the definition location
  definition_locations_[key] = range;

  // Also add the reference to the reference map
  reference_map_[range] = key;
}

void SymbolIndex::AddReference(
    const slang::SourceRange& range, const SymbolKey& key) {
  reference_map_[range] = key;
}

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

        const auto& definition = symbol.getDefinition();
        SymbolKey key = SymbolKey::FromSourceLocation(definition.location);

        if (const auto* syntax_node = definition.getSyntax()) {
          if (syntax_node->kind ==
              slang::syntax::SyntaxKind::ModuleDeclaration) {
            const auto& module_syntax =
                syntax_node->as<slang::syntax::ModuleDeclarationSyntax>();

            const auto& range = module_syntax.header->name.range();
            index.AddDefinition(key, range);

            // Handle endmodule name if present
            if (const auto& end_name = module_syntax.blockName) {
              index.AddReference(end_name->name.range(), key);
            }
          }
        }

        self.visitDefault(symbol);
      },

      // Variable definition visitor
      [&](auto& self, const slang::ast::VariableSymbol& symbol) {
        spdlog::debug("SymbolIndex visiting variable symbol {}", symbol.name);

        const auto& loc = symbol.location;
        SymbolKey key = SymbolKey::FromSourceLocation(loc);

        if (const auto& symbol_syntax = symbol.getSyntax()) {
          index.AddDefinition(key, symbol_syntax->sourceRange());
        }

        self.visitDefault(symbol);
      },

      // Parameter definition visitor
      [&](auto& self, const slang::ast::ParameterSymbol& symbol) {
        spdlog::debug("SymbolIndex visiting parameter symbol {}", symbol.name);

        const auto& loc = symbol.location;
        SymbolKey key = SymbolKey::FromSourceLocation(loc);

        if (const auto& symbol_syntax = symbol.getSyntax()) {
          index.AddDefinition(key, symbol_syntax->sourceRange());
        }

        self.visitDefault(symbol);
      },

      // Named value reference visitor
      [&](auto& self, const slang::ast::NamedValueExpression& expr) {
        spdlog::debug(
            "SymbolIndex visiting named value expression {}", expr.symbol.name);

        const auto& loc = expr.symbol.location;
        const auto& range = expr.sourceRange;

        SymbolKey key = SymbolKey::FromSourceLocation(loc);

        index.AddReference(range, key);
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

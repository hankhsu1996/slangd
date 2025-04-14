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
  definition_locations_[key] = range;
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

        const auto& loc = symbol.location;
        bool has_valid_location = loc && loc.buffer().valid();

        // Only add instance definitions for valid instances with names and real
        // locations
        if (has_valid_location && !symbol.name.empty()) {
          SymbolKey key{
              .bufferId = loc.buffer().getId(), .offset = loc.offset()};

          // Create a range using the symbol name length
          size_t name_length = symbol.name.length();
          auto end_loc =
              slang::SourceLocation(loc.buffer(), loc.offset() + name_length);
          slang::SourceRange symbol_range(loc, end_loc);

          index.AddDefinition(key, symbol_range);
          index.AddReference(symbol_range, key);
        }

        // Handle module declaration syntax to find endmodule names
        // (this works even for instances with placeholder locations)
        const auto& definition = symbol.getDefinition();

        if (const auto* syntax_node = definition.getSyntax()) {
          if (syntax_node->kind ==
              slang::syntax::SyntaxKind::ModuleDeclaration) {
            const auto& module_syntax =
                syntax_node->as<slang::syntax::ModuleDeclarationSyntax>();

            // Get the header name (module declaration name)
            const auto& header_name = module_syntax.header->name;
            auto module_name_range = header_name.range();

            // Create a key and definition for the module declaration
            SymbolKey module_key{
                .bufferId = definition.location.buffer().getId(),
                .offset = definition.location.offset()};

            // Only add the definition if not already present
            if (index.GetDefinitionRange(module_key) == std::nullopt) {
              index.AddDefinition(module_key, module_name_range);
              index.AddReference(module_name_range, module_key);
            }

            // Handle endmodule name if present
            if (const auto& end_name = module_syntax.blockName) {
              auto end_name_range = end_name->name.range();

              // Skip invalid buffers
              if (!end_name_range.start().buffer().valid()) {
                spdlog::debug("End module name has invalid buffer, skipping");
                self.visitDefault(symbol);
                return;
              }

              index.AddReference(end_name_range, module_key);
            }
          }
        }

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

        index.AddDefinition(key, symbol_range);
        index.AddReference(symbol_range, key);
        self.visitDefault(symbol);
      },

      // Parameter definition visitor
      [&](auto& self, const slang::ast::ParameterSymbol& symbol) {
        spdlog::debug("SymbolIndex visiting parameter symbol {}", symbol.name);

        const auto& loc = symbol.location;
        SymbolKey key{.bufferId = loc.buffer().getId(), .offset = loc.offset()};

        // Create a range using the symbol name length
        size_t name_length = symbol.name.length();
        auto end_loc =
            slang::SourceLocation(loc.buffer(), loc.offset() + name_length);
        slang::SourceRange symbol_range(loc, end_loc);

        index.AddDefinition(key, symbol_range);
        index.AddReference(symbol_range, key);
        self.visitDefault(symbol);
      },

      // Named value reference visitor
      [&](auto& self, const slang::ast::NamedValueExpression& expr) {
        spdlog::debug(
            "SymbolIndex visiting named value expression {}", expr.symbol.name);

        const auto& loc = expr.symbol.location;
        const auto& range = expr.sourceRange;

        SymbolKey key{.bufferId = loc.buffer().getId(), .offset = loc.offset()};

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

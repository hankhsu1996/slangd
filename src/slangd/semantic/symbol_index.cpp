// symbol_index.cpp

#include "slangd/semantic/symbol_index.hpp"

#include <cassert>

#include <spdlog/spdlog.h>

#include "slang/ast/ASTVisitor.h"
#include "slang/ast/expressions/MiscExpressions.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/syntax/AllSyntax.h"
#include "slang/text/SourceManager.h"
#include "slangd/utils/source_utils.hpp"
#include "slangd/utils/uri.hpp"

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

namespace {
// Indexing helper functions

[[maybe_unused]] void IndexPackage(
    const slang::ast::PackageSymbol& symbol, SymbolIndex& index) {
  SymbolKey key = SymbolKey::FromSourceLocation(symbol.location);

  if (const auto* syntax_node = symbol.getSyntax()) {
    if (syntax_node->kind == slang::syntax::SyntaxKind::PackageDeclaration) {
      // Module and package both use ModuleDeclarationSyntax
      const auto& package_syntax =
          syntax_node->as<slang::syntax::ModuleDeclarationSyntax>();

      const auto& range = package_syntax.header->name.range();
      index.AddDefinition(key, range);

      // Handle endpackage name if present
      if (const auto& end_name = package_syntax.blockName) {
        index.AddReference(end_name->name.range(), key);
      }
    }
  }
}

[[maybe_unused]] void IndexDefinition(
    const slang::ast::DefinitionSymbol& def, SymbolIndex& index,
    slang::ast::Compilation& compilation) {
  const auto* syntax = def.getSyntax();
  if ((syntax != nullptr) &&
      syntax->kind == slang::syntax::SyntaxKind::ModuleDeclaration) {
    // Add the definition to the index
    const auto& module_syntax =
        syntax->as<slang::syntax::ModuleDeclarationSyntax>();
    const auto& module_range = module_syntax.header->name.range();
    SymbolKey key = SymbolKey::FromSourceLocation(def.location);
    index.AddDefinition(key, module_range);

    // Add the endmodule label to the reference map
    if (const auto& end_label = module_syntax.blockName) {
      index.AddReference(end_label->name.range(), key);
    }

    // Handle imports
    for (const auto& import_decl : module_syntax.header->imports) {
      for (const auto& import_item : import_decl->items) {
        if (import_item->kind == slang::syntax::SyntaxKind::PackageImportItem) {
          const auto& import_syntax =
              import_item->as<slang::syntax::PackageImportItemSyntax>();
          auto package_range = import_syntax.package.range();
          const auto& package_name = import_syntax.package.valueText();
          const auto* package_symbol = compilation.getPackage(package_name);
          if (package_symbol != nullptr) {
            SymbolKey key =
                SymbolKey::FromSourceLocation(package_symbol->location);
            index.AddReference(package_range, key);
          }
        }
      }
    }
  }
}

[[maybe_unused]] void IndexInstance(
    const slang::ast::InstanceSymbol& symbol, SymbolIndex& index) {
  const auto& def = symbol.getDefinition();
  const auto& loc = def.location;
  SymbolKey key = SymbolKey::FromSourceLocation(loc);

  if (const auto& symbol_syntax = symbol.getSyntax()) {
    if (symbol_syntax->kind !=
        slang::syntax::SyntaxKind::HierarchicalInstance) {
      return;
    }

    const auto& instance_syntax =
        symbol_syntax->as<slang::syntax::HierarchicalInstanceSyntax>();

    if (instance_syntax.parent == nullptr ||
        instance_syntax.parent->kind !=
            slang::syntax::SyntaxKind::HierarchyInstantiation) {
      return;
    }

    const auto& instantiation_syntax =
        instance_syntax.parent
            ->as<slang::syntax::HierarchyInstantiationSyntax>();
    auto ref_range = instantiation_syntax.type.range();
    index.AddReference(ref_range, key);
  }
}

[[maybe_unused]] void IndexTypeAlias(
    const slang::ast::TypeAliasType& symbol, SymbolIndex& index) {
  const auto& loc = symbol.location;
  SymbolKey key = SymbolKey::FromSourceLocation(loc);

  if (const auto& symbol_syntax = symbol.getSyntax()) {
    if (symbol_syntax->kind == slang::syntax::SyntaxKind::TypedefDeclaration) {
      const auto& type_alias_syntax =
          symbol_syntax->as<slang::syntax::TypedefDeclarationSyntax>();
      index.AddDefinition(key, type_alias_syntax.name.range());
    }
  }

  // If it's an enum type, process all enum values
  if (symbol.targetType.getType().kind == slang::ast::SymbolKind::EnumType) {
    const auto& enum_type =
        symbol.targetType.getType().as<slang::ast::EnumType>();
    for (const auto& enum_value : enum_type.values()) {
      auto key = SymbolKey::FromSourceLocation(enum_value.location);
      if (const auto& value_syntax = enum_value.getSyntax()) {
        index.AddDefinition(key, value_syntax->sourceRange());
      }
    }
  }
}

[[maybe_unused]] void IndexVariable(
    const slang::ast::VariableSymbol& symbol, SymbolIndex& index) {
  const auto& loc = symbol.location;
  SymbolKey key = SymbolKey::FromSourceLocation(loc);

  if (const auto& symbol_syntax = symbol.getSyntax()) {
    index.AddDefinition(key, symbol_syntax->sourceRange());
  }

  // Variable type reference
  const auto& declared_type = symbol.getDeclaredType();
  if (const auto& type_syntax = declared_type->getTypeSyntax()) {
    if (type_syntax->kind == slang::syntax::SyntaxKind::NamedType) {
      const auto& named_type =
          type_syntax->as<slang::syntax::NamedTypeSyntax>();
      const auto& resolved_type = symbol.getType();
      SymbolKey type_key =
          SymbolKey::FromSourceLocation(resolved_type.location);
      index.AddReference(named_type.name->sourceRange(), type_key);
    }
  }
}

[[maybe_unused]] void IndexParameter(
    const slang::ast::ParameterSymbol& symbol, SymbolIndex& index) {
  const auto& loc = symbol.location;
  SymbolKey key = SymbolKey::FromSourceLocation(loc);

  if (const auto& symbol_syntax = symbol.getSyntax()) {
    index.AddDefinition(key, symbol_syntax->sourceRange());
  }

  // Parameter type reference
  const auto& declared_type = symbol.getDeclaredType();
  if (const auto& type_syntax = declared_type->getTypeSyntax()) {
    if (type_syntax->kind == slang::syntax::SyntaxKind::NamedType) {
      const auto& named_type =
          type_syntax->as<slang::syntax::NamedTypeSyntax>();
      const auto& resolved_type = symbol.getType();
      SymbolKey type_key =
          SymbolKey::FromSourceLocation(resolved_type.location);
      index.AddReference(named_type.name->sourceRange(), type_key);
    }
  }
}

[[maybe_unused]] void IndexPort(
    const slang::ast::PortSymbol& port, SymbolIndex& index) {
  const auto& declared_type =
      port.internalSymbol->as<slang::ast::ValueSymbol>().getDeclaredType();

  SymbolKey key = SymbolKey::FromSourceLocation(port.getType().location);

  if (const auto& type_alias = declared_type->getTypeSyntax()) {
    if (type_alias->kind == slang::syntax::SyntaxKind::NamedType) {
      const auto& named_type = type_alias->as<slang::syntax::NamedTypeSyntax>();
      const auto& name = named_type.name;
      const auto& range = name->sourceRange();
      index.AddReference(range, key);
    }
  }
}

[[maybe_unused]] void IndexNamedValue(
    const slang::ast::NamedValueExpression& expr, SymbolIndex& index) {
  const auto& loc = expr.symbol.location;
  const auto& range = expr.sourceRange;

  SymbolKey key = SymbolKey::FromSourceLocation(loc);

  index.AddReference(range, key);
}

[[maybe_unused]] void IndexStatementBlock(
    const slang::ast::StatementBlockSymbol& symbol, SymbolIndex& index) {
  if (!symbol.name.empty()) {
    SymbolKey key = SymbolKey::FromSourceLocation(symbol.location);
    if (const auto* syntax = symbol.getSyntax()) {
      if (syntax->kind == slang::syntax::SyntaxKind::SequentialBlockStatement ||
          syntax->kind == slang::syntax::SyntaxKind::ParallelBlockStatement) {
        const auto& block_syntax =
            syntax->as<slang::syntax::BlockStatementSyntax>();
        if (block_syntax.blockName != nullptr) {
          index.AddDefinition(key, block_syntax.blockName->name.range());
        }
      }
    }
  }
}

}  // anonymous namespace

auto SymbolIndex::FromCompilation(
    slang::ast::Compilation& compilation,
    const std::unordered_set<std::string>& traverse_paths,
    std::shared_ptr<spdlog::logger> logger) -> SymbolIndex {
  SymbolIndex index(compilation, logger);
  auto visitor = slang::ast::makeVisitor(
      // Special handling for instance body
      [&](auto& self, const slang::ast::InstanceBodySymbol& symbol) {
        self.visitDefault(symbol);
      },

      // Package definition visitor
      [&](auto& self, const slang::ast::PackageSymbol& symbol) {
        IndexPackage(symbol, index);

        self.visitDefault(symbol);
      },

      // Module/interface definition visitor
      [&](auto& self, const slang::ast::DefinitionSymbol& def) {
        IndexDefinition(def, index, compilation);

        // Check if we should traverse the instance body
        auto path = NormalizePath(UriToPath(std::string(
            compilation.getSourceManager()->getFileName(def.location))));
        auto should_traverse =
            traverse_paths.find(path) != traverse_paths.end();

        if (should_traverse &&
            (def.definitionKind == slang::ast::DefinitionKind::Module ||
             def.definitionKind == slang::ast::DefinitionKind::Interface)) {
          const auto& instance =
              slang::ast::InstanceSymbol::createDefault(compilation, def);
          instance.body.visit(self);
        }
      },

      // Module instance visitor
      [&](auto& self, const slang::ast::InstanceSymbol& symbol) {
        IndexInstance(symbol, index);
        self.visitDefault(symbol);
      },

      // Type alias definition visitor
      [&](auto& self, const slang::ast::TypeAliasType& symbol) {
        IndexTypeAlias(symbol, index);
        self.visitDefault(symbol);
      },

      // Variable definition visitor
      [&](auto& self, const slang::ast::VariableSymbol& symbol) {
        IndexVariable(symbol, index);
        self.visitDefault(symbol);
      },

      // Parameter definition visitor
      [&](auto& self, const slang::ast::ParameterSymbol& symbol) {
        IndexParameter(symbol, index);
        self.visitDefault(symbol);
      },

      // Port list type reference visitor
      [&](auto& self, const slang::ast::PortSymbol& port) {
        IndexPort(port, index);
        self.visitDefault(port);
      },

      // Named value reference visitor
      [&](auto& self, const slang::ast::NamedValueExpression& expr) {
        IndexNamedValue(expr, index);
        self.visitDefault(expr);
      },

      // Procedural block label visitor
      [&](auto& self, const slang::ast::StatementBlockSymbol& symbol) {
        IndexStatementBlock(symbol, index);
        self.visitDefault(symbol);
      });

  // Visit all definitions
  for (const auto* symbol : compilation.getDefinitions()) {
    if (symbol != nullptr) {
      symbol->visit(visitor);
    }
  }

  // Visit all packages
  for (const auto* package_symbol : compilation.getPackages()) {
    if (package_symbol != nullptr) {
      package_symbol->visit(visitor);
    }
  }

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

auto SymbolIndex::PrintInfo() const -> void {
  logger_->info("SymbolIndex info:");
  logger_->info("  Definition locations: {}", definition_locations_.size());
  logger_->info("  Reference map: {}", reference_map_.size());
}

}  // namespace slangd::semantic

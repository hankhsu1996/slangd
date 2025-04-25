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

[[maybe_unused]] void IndexUninstantiatedDef(
    const slang::ast::UninstantiatedDefSymbol& symbol, SymbolIndex& index,
    slang::ast::Compilation& compilation) {
  // Get the instance type location
  slang::SourceRange instance_type_range;
  if (symbol.getSyntax()->kind ==
      slang::syntax::SyntaxKind::HierarchicalInstance) {
    const auto& hierarchical_instance_syntax =
        symbol.getSyntax()->as<slang::syntax::HierarchicalInstanceSyntax>();
    // Try get parent and convert to HierarchicalInstantiationSyntax to get the
    // type range
    if (const auto* parent = hierarchical_instance_syntax.parent) {
      if (parent->kind == slang::syntax::SyntaxKind::HierarchyInstantiation) {
        instance_type_range =
            parent->as<slang::syntax::HierarchyInstantiationSyntax>()
                .type.range();
      }
    }
  }

  // Lookup for the module definition. If found, add the instance type to
  // the reference map
  const auto* scope = symbol.getParentScope();
  if (scope != nullptr) {
    auto module_def =
        compilation.tryGetDefinition(symbol.definitionName, *scope);
    if (module_def.definition != nullptr) {
      const auto* syntax = module_def.definition->getSyntax();
      if (syntax->kind == slang::syntax::SyntaxKind::ModuleDeclaration) {
        const auto& module_syntax =
            syntax->as<slang::syntax::ModuleDeclarationSyntax>();
        auto module_def_location = module_syntax.header->name.range().start();
        SymbolKey key = SymbolKey::FromSourceLocation(module_def_location);

        index.AddReference(instance_type_range, key);
      }
    }
  }

  // Add the instance name to the definition map
  if (const auto& symbol_syntax = symbol.getSyntax()) {
    if (symbol_syntax->kind ==
        slang::syntax::SyntaxKind::HierarchicalInstance) {
      const auto& instance_syntax =
          symbol_syntax->as<slang::syntax::HierarchicalInstanceSyntax>();

      // Instance name definition
      if (instance_syntax.decl != nullptr) {
        SymbolKey key = SymbolKey::FromSourceLocation(symbol.location);
        index.AddDefinition(key, instance_syntax.decl->name.range());
      }
    }
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

  // Port definition type reference
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
    const std::unordered_set<slang::BufferID>& traverse_buffers,
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
        logger->debug("SymbolIndex indexing definition: {}", def.name);

        // Check if we should traverse the instance body
        auto def_buffer = def.location.buffer();
        auto should_traverse =
            traverse_buffers.find(def_buffer) != traverse_buffers.end();

        if (should_traverse &&
            (def.definitionKind == slang::ast::DefinitionKind::Module ||
             def.definitionKind == slang::ast::DefinitionKind::Interface)) {
          logger->debug("SymbolIndex traversing instance body: {}", def.name);
          const auto& instance =
              slang::ast::InstanceSymbol::createInvalid(compilation, def);
          instance.body.visit(self);
        }
      },

      // UninstantiatedDefSymbol visitor
      [&](auto& self, const slang::ast::UninstantiatedDefSymbol& symbol) {
        IndexUninstantiatedDef(symbol, index, compilation);
        for (const auto& port_connection : symbol.getPortConnections()) {
          if (port_connection != nullptr) {
            port_connection->visit(self);
          }
        }
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
  logger_->info(
      "  Definition locations size: {}", definition_locations_.size());
  for (const auto& [key, range] : definition_locations_) {
    logger_->info(
        "    {}:{} -> {}:{}-{}:{}", key.bufferId, key.offset,
        range.start().buffer().getId(), range.start().offset(),
        range.end().buffer().getId(), range.end().offset());
  }
  logger_->info("  Reference map size: {}", reference_map_.size());
  for (const auto& [range, key] : reference_map_) {
    logger_->info(
        "    {}:{}-{}:{} -> {}:{}", range.start().buffer().getId(),
        range.start().offset(), range.end().buffer().getId(),
        range.end().offset(), key.bufferId, key.offset);
  }
}

}  // namespace slangd::semantic

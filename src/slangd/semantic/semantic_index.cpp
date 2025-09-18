#include "slangd/semantic/semantic_index.hpp"

#include <slang/ast/Compilation.h>
#include <slang/ast/Symbol.h>
#include <slang/ast/expressions/MiscExpressions.h>
#include <slang/ast/symbols/CompilationUnitSymbols.h>
#include <slang/ast/symbols/InstanceSymbols.h>
#include <slang/ast/symbols/MemberSymbols.h>
#include <slang/ast/symbols/ParameterSymbols.h>
#include <slang/ast/symbols/VariableSymbols.h>
#include <slang/ast/types/AllTypes.h>
#include <slang/syntax/AllSyntax.h>

#include "slangd/utils/conversion.hpp"
#include "slangd/utils/path_utils.hpp"

namespace slangd::semantic {

auto SemanticIndex::FromCompilation(
    slang::ast::Compilation& compilation,
    const slang::SourceManager& source_manager)
    -> std::unique_ptr<SemanticIndex> {
  auto index = std::unique_ptr<SemanticIndex>(new SemanticIndex());

  // Store source manager reference for document symbol processing
  index->source_manager_ = &source_manager;

  // Create visitor for comprehensive symbol collection and reference tracking
  auto visitor = IndexVisitor(index.get(), &source_manager);

  // Single traversal from root captures ALL symbols
  compilation.getRoot().visit(visitor);

  return index;
}

auto SemanticIndex::GetSymbolAt(slang::SourceLocation location) const
    -> std::optional<SymbolInfo> {
  if (auto it = symbols_.find(location); it != symbols_.end()) {
    return it->second;
  }
  return std::nullopt;
}

auto SemanticIndex::GetAllSymbols() const
    -> const std::unordered_map<slang::SourceLocation, SymbolInfo>& {
  return symbols_;
}

auto SemanticIndex::GetDocumentSymbols(const std::string& uri) const
    -> std::vector<lsp::DocumentSymbol> {
  return BuildDocumentSymbolTree(uri);
}

// Utility method implementations
auto SemanticIndex::UnwrapSymbol(const slang::ast::Symbol& symbol)
    -> const slang::ast::Symbol& {
  using SK = slang::ast::SymbolKind;

  // If not a TransparentMember, return directly
  if (symbol.kind != SK::TransparentMember) {
    return symbol;
  }

  // Recursively unwrap
  return UnwrapSymbol(symbol.as<slang::ast::TransparentMemberSymbol>().wrapped);
}

auto SemanticIndex::ConvertToLspKind(const slang::ast::Symbol& symbol)
    -> lsp::SymbolKind {
  using SK = slang::ast::SymbolKind;
  using LK = lsp::SymbolKind;
  using DK = slang::ast::DefinitionKind;

  // Handle type alias first
  if (symbol.kind == SK::TypeAlias) {
    const auto& type_alias = symbol.as<slang::ast::TypeAliasType>();
    const auto& canonical_type = type_alias.getCanonicalType();
    switch (canonical_type.kind) {
      case SK::EnumType:
        return LK::kEnum;
      case SK::PackedStructType:
      case SK::UnpackedStructType:
      case SK::PackedUnionType:
      case SK::UnpackedUnionType:
        return LK::kStruct;
      default:
        return LK::kTypeParameter;
    }
  }

  // Handle Definition symbols
  if (symbol.kind == SK::Definition) {
    const auto& definition = symbol.as<slang::ast::DefinitionSymbol>();
    if (definition.definitionKind == DK::Module) {
      // In SystemVerilog, a 'module' defines encapsulated hardware with ports
      // and internal logic. However, in software terms, it behaves more like a
      // 'class': it has state, methods (processes), and can be instantiated
      // multiple times. It's not just a namespace or file like software
      // modules.
      return LK::kClass;
    }
    if (definition.definitionKind == DK::Interface) {
      return LK::kInterface;
    }
  }

  // For non-Definition symbols, use a switch on the symbol kind
  switch (symbol.kind) {
    // Package
    case SK::Package:
      return LK::kPackage;

    // InstanceBody represents instantiated module/interface content
    // For universal collection, we need to distinguish interface from module
    // bodies
    case SK::InstanceBody: {
      // Check if this InstanceBody represents an interface by looking for
      // interface indicators Interfaces typically contain modports, modules
      // typically don't
      const auto& instance_body = symbol.as<slang::ast::InstanceBodySymbol>();

      // Look through the body members to detect interface patterns
      if (instance_body.isScope()) {
        const auto& scope = instance_body.as<slang::ast::Scope>();
        for (const auto& member : scope.members()) {
          if (member.kind == SK::Modport) {
            // Found a modport - this is likely an interface
            return LK::kInterface;
          }
        }
      }

      // If no interface indicators found, assume it's a module
      return LK::kClass;
    }

    // Variables and data
    case SK::Variable:
    case SK::Net:
    case SK::Port:
    case SK::Instance:
    case SK::UninstantiatedDef:
      return LK::kVariable;

    case SK::Field:
    case SK::ClassProperty:
      return LK::kField;

    case SK::Parameter:
      return LK::kConstant;

    case SK::EnumValue:
      return LK::kEnumMember;

    case SK::TypeParameter:
      return LK::kTypeParameter;

    // Type-related
    case SK::TypeAlias:
    case SK::ForwardingTypedef:
      return LK::kTypeParameter;

    case SK::EnumType:
      return LK::kEnum;

    case SK::PackedStructType:
    case SK::UnpackedStructType:
      return LK::kStruct;

    case SK::PackedUnionType:
    case SK::UnpackedUnionType:
      return LK::kClass;

    case SK::ClassType:
      return LK::kClass;

    // Interface-related
    case SK::Modport:
      return LK::kInterface;

    // Function-related
    case SK::Subroutine:
      return LK::kFunction;

    // Default for other symbol kinds
    default:
      return LK::kObject;
  }
}

auto SemanticIndex::ComputeLspRange(
    const slang::ast::Symbol& symbol,
    const slang::SourceManager& source_manager) -> lsp::Range {
  if (symbol.location) {
    return ConvertSlangLocationToLspRange(symbol.location, source_manager);
  }
  // Return zero range for symbols without location
  return lsp::Range{
      .start = {.line = 0, .character = 0}, .end = {.line = 0, .character = 0}};
}

auto SemanticIndex::ShouldIndex(const slang::ast::Symbol& symbol) -> bool {
  // Skip symbols without names (except some special cases)
  if (symbol.name.empty()) {
    using SK = slang::ast::SymbolKind;
    // Allow some unnamed symbols that are still useful
    switch (symbol.kind) {
      case SK::CompilationUnit:
      case SK::InstanceBody:
      case SK::Instance:
        break;  // Allow these unnamed symbols
      default:
        return false;  // Skip other unnamed symbols
    }
  }

  // Skip symbols without valid locations
  if (!symbol.location.valid()) {
    return false;
  }

  return true;
}

auto SemanticIndex::BuildDocumentSymbolTree(const std::string& uri) const
    -> std::vector<lsp::DocumentSymbol> {
  // Build parent-to-children map from flat symbols_, filtering by URI
  std::unordered_map<const slang::ast::Scope*, std::vector<const SymbolInfo*>>
      children_map;
  std::vector<const SymbolInfo*> roots;

  // Group symbols by their parent, filtering by URI
  for (const auto& [location, info] : symbols_) {
    // Skip EnumValue symbols at root level - they'll be added as children
    if (info.symbol->kind == slang::ast::SymbolKind::EnumValue) {
      continue;
    }

    // FILTER: Only include symbols from the requested document
    if (!IsLocationInDocument(info.location, *source_manager_, uri)) {
      continue;
    }

    // Treat top-level symbols as roots, others as children
    // Package, Module, Interface are typically top-level even with non-null
    // parent
    if (info.parent == nullptr ||
        info.symbol->kind == slang::ast::SymbolKind::Package ||
        info.symbol->kind == slang::ast::SymbolKind::InstanceBody) {
      roots.push_back(&info);
    } else {
      children_map[info.parent].push_back(&info);
    }
  }

  // Debug: If no roots found, check what parents we have
  if (roots.empty()) {
    // Find symbols that might be top-level based on context
    for (const auto& [location, info] : symbols_) {
      if (info.symbol->kind == slang::ast::SymbolKind::EnumValue) {
        continue;
      }

      // FILTER: Only include symbols from the requested document
      if (!IsLocationInDocument(info.location, *source_manager_, uri)) {
        continue;
      }

      // For modules/interfaces (InstanceBody), treat as root even with parent
      if (info.symbol->kind == slang::ast::SymbolKind::InstanceBody) {
        roots.push_back(&info);
      }
    }
  }

  // Recursively build DocumentSymbol tree from roots
  std::vector<lsp::DocumentSymbol> result;
  for (const auto* root_info : roots) {
    auto doc_symbol = CreateDocumentSymbol(*root_info);

    // For symbols, we need to check if the symbol itself is a scope
    const slang::ast::Scope* symbol_as_scope = nullptr;
    if (root_info->symbol->isScope()) {
      symbol_as_scope = &root_info->symbol->as<slang::ast::Scope>();
    }
    AttachChildrenToSymbol(doc_symbol, symbol_as_scope, children_map);

    // Special handling for enum type aliases
    if (root_info->symbol->kind == slang::ast::SymbolKind::TypeAlias) {
      HandleEnumTypeAlias(doc_symbol, root_info->symbol);
    }

    result.push_back(std::move(doc_symbol));
  }

  return result;
}

auto SemanticIndex::CreateDocumentSymbol(const SymbolInfo& info)
    -> lsp::DocumentSymbol {
  lsp::DocumentSymbol doc_symbol;
  doc_symbol.name = std::string(info.symbol->name);
  doc_symbol.kind = info.lsp_kind;
  doc_symbol.range = info.range;
  doc_symbol.selectionRange = info.range;  // Use same range for now
  doc_symbol.children = std::vector<lsp::DocumentSymbol>();
  return doc_symbol;
}

auto SemanticIndex::AttachChildrenToSymbol(
    lsp::DocumentSymbol& parent, const slang::ast::Scope* parent_scope,
    const std::unordered_map<
        const slang::ast::Scope*, std::vector<const SymbolInfo*>>& children_map)
    const -> void {
  if (parent_scope == nullptr) {
    return;  // No scope to check for children
  }

  auto children_it = children_map.find(parent_scope);
  if (children_it == children_map.end()) {
    return;  // No children
  }

  for (const auto* child_info : children_it->second) {
    auto child_doc_symbol = CreateDocumentSymbol(*child_info);

    // For child symbols, check if they are scopes themselves
    const slang::ast::Scope* child_as_scope = nullptr;
    if (child_info->symbol->isScope()) {
      child_as_scope = &child_info->symbol->as<slang::ast::Scope>();
    }
    AttachChildrenToSymbol(child_doc_symbol, child_as_scope, children_map);

    // Special handling for enum type aliases in children too
    if (child_info->symbol->kind == slang::ast::SymbolKind::TypeAlias) {
      HandleEnumTypeAlias(child_doc_symbol, child_info->symbol);
    }

    parent.children->push_back(std::move(child_doc_symbol));
  }
}

auto SemanticIndex::HandleEnumTypeAlias(
    lsp::DocumentSymbol& enum_doc_symbol,
    const slang::ast::Symbol* type_alias_symbol) const -> void {
  using SK = slang::ast::SymbolKind;

  // Check if this is a TypeAlias of an enum
  if (type_alias_symbol->kind != SK::TypeAlias) {
    return;
  }

  const auto& type_alias = type_alias_symbol->as<slang::ast::TypeAliasType>();
  const auto& canonical_type = type_alias.getCanonicalType();

  if (canonical_type.kind != SK::EnumType) {
    return;  // Not an enum
  }

  // Get the enum type to access its values directly
  const auto& enum_type = canonical_type.as<slang::ast::EnumType>();

  // Use the enum type's values() method to get all enum values
  // This is more reliable than trying to match by scope
  for (const auto& enum_value : enum_type.values()) {
    // Create a SymbolInfo-like structure for the enum value
    lsp::DocumentSymbol enum_value_symbol;
    enum_value_symbol.name = std::string(enum_value.name);
    enum_value_symbol.kind = lsp::SymbolKind::kEnumMember;

    // Set range from the enum value location
    if (enum_value.location && (source_manager_ != nullptr)) {
      enum_value_symbol.range = ComputeLspRange(enum_value, *source_manager_);
      enum_value_symbol.selectionRange = enum_value_symbol.range;
    } else {
      // Default range if no location
      enum_value_symbol.range = {
          .start = {.line = 0, .character = 0},
          .end = {.line = 0, .character = 0}};
      enum_value_symbol.selectionRange = {
          .start = {.line = 0, .character = 0},
          .end = {.line = 0, .character = 0}};
    }

    enum_value_symbol.children = std::vector<lsp::DocumentSymbol>();
    enum_doc_symbol.children->push_back(std::move(enum_value_symbol));
  }
}

auto SemanticIndex::ExtractDefinitionRange(
    const slang::ast::Symbol& symbol, const slang::syntax::SyntaxNode& syntax)
    -> slang::SourceRange {
  using SK = slang::ast::SymbolKind;
  using SyntaxKind = slang::syntax::SyntaxKind;

  // Extract precise name range based on symbol and syntax type
  // This is adapted from the patterns in legacy definition_index.cpp

  switch (symbol.kind) {
    case SK::Package:
      if (syntax.kind == SyntaxKind::PackageDeclaration) {
        const auto& pkg_syntax =
            syntax.as<slang::syntax::ModuleDeclarationSyntax>();
        return pkg_syntax.header->name.range();
      }
      break;

    case SK::Definition: {
      if (syntax.kind == SyntaxKind::ModuleDeclaration) {
        const auto& mod_syntax =
            syntax.as<slang::syntax::ModuleDeclarationSyntax>();
        return mod_syntax.header->name.range();
      }
      break;
    }

    case SK::TypeAlias:
      if (syntax.kind == SyntaxKind::TypedefDeclaration) {
        const auto& typedef_syntax =
            syntax.as<slang::syntax::TypedefDeclarationSyntax>();
        return typedef_syntax.name.range();
      }
      break;

    case SK::Variable:
    case SK::Parameter:
      // For variables and parameters, use the entire syntax range as name range
      return syntax.sourceRange();

    case SK::StatementBlock: {
      if (syntax.kind == SyntaxKind::SequentialBlockStatement ||
          syntax.kind == SyntaxKind::ParallelBlockStatement) {
        const auto& block_syntax =
            syntax.as<slang::syntax::BlockStatementSyntax>();
        if (block_syntax.blockName != nullptr) {
          return block_syntax.blockName->name.range();
        }
      }
      break;
    }

    default:
      // For most symbol types, use the syntax source range
      break;
  }

  // Default fallback: use the syntax node's source range
  return syntax.sourceRange();
}

// IndexVisitor implementation
void SemanticIndex::IndexVisitor::ProcessSymbol(
    const slang::ast::Symbol& symbol) {
  // Only index symbols that meet basic criteria
  if (!SemanticIndex::ShouldIndex(symbol)) {
    return;
  }

  // Unwrap symbol to handle TransparentMember recursion
  const auto& unwrapped = SemanticIndex::UnwrapSymbol(symbol);

  // Extract precise definition range from syntax node
  slang::SourceRange definition_range;
  bool is_definition = false;
  if (const auto* syntax = unwrapped.getSyntax()) {
    definition_range =
        SemanticIndex::ExtractDefinitionRange(unwrapped, *syntax);
    is_definition = true;
  } else {
    // Fallback to symbol location for symbols without syntax
    definition_range =
        slang::SourceRange{unwrapped.location, unwrapped.location};
    is_definition = false;
  }

  // Create SymbolInfo with cached LSP data
  SymbolInfo info{
      .symbol = &unwrapped,
      .location = unwrapped.location,
      .lsp_kind = SemanticIndex::ConvertToLspKind(unwrapped),
      .range = SemanticIndex::ComputeLspRange(unwrapped, *source_manager_),
      .parent = unwrapped.getParentScope(),
      .is_definition = is_definition,
      .definition_range = definition_range,
      .buffer_id = unwrapped.location.buffer().getId()};

  // Store in flat map for O(1) lookup
  index_->symbols_[unwrapped.location] = info;

  // Store definition range for go-to-definition API
  if (is_definition) {
    SymbolKey key = SymbolKey::FromSourceLocation(unwrapped.location);
    index_->definition_ranges_[key] = definition_range;
  }
}

void SemanticIndex::IndexVisitor::handle(
    const slang::ast::NamedValueExpression& expr) {
  // Track reference: expr.sourceRange -> expr.symbol.location
  if (expr.symbol.location.valid()) {
    SymbolKey key = SymbolKey::FromSourceLocation(expr.symbol.location);
    index_->reference_map_[expr.sourceRange] = key;
  }

  // Continue traversal
  this->visitDefault(expr);
}

void SemanticIndex::IndexVisitor::handle(
    const slang::ast::VariableSymbol& symbol) {
  // Track type references in variable declarations (e.g., data_t my_data;)
  const auto& declared_type = symbol.getDeclaredType();
  if (const auto& type_syntax = declared_type->getTypeSyntax()) {
    if (type_syntax->kind == slang::syntax::SyntaxKind::NamedType) {
      const auto& named_type =
          type_syntax->as<slang::syntax::NamedTypeSyntax>();
      const auto& resolved_type = symbol.getType();

      if (resolved_type.location.valid()) {
        SymbolKey type_key =
            SymbolKey::FromSourceLocation(resolved_type.location);
        index_->reference_map_[named_type.name->sourceRange()] = type_key;
      }
    }
  }

  // Continue traversal
  this->visitDefault(symbol);
}

auto SemanticIndex::GetDefinitionRange(const SymbolKey& key) const
    -> std::optional<slang::SourceRange> {
  auto it = definition_ranges_.find(key);
  if (it != definition_ranges_.end()) {
    return it->second;
  }
  return std::nullopt;
}

auto SemanticIndex::LookupSymbolAt(slang::SourceLocation loc) const
    -> std::optional<SymbolKey> {
  // O(n) search through reference map - matches legacy DefinitionIndex behavior
  for (const auto& [range, key] : reference_map_) {
    if (range.contains(loc)) {
      return key;
    }
  }
  return std::nullopt;
}

}  // namespace slangd::semantic

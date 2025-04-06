#include "slangd/features/symbols.hpp"

#include <unordered_set>

#include <slang/ast/Scope.h>
#include <slang/ast/Symbol.h>
#include <slang/ast/symbols/CompilationUnitSymbols.h>
#include <slang/ast/symbols/InstanceSymbols.h>
#include <slang/ast/symbols/MemberSymbols.h>
#include <slang/ast/symbols/ParameterSymbols.h>
#include <slang/ast/symbols/VariableSymbols.h>
#include <slang/ast/types/AllTypes.h>
#include <slang/ast/types/Type.h>
#include <slang/text/SourceManager.h>

#include "slangd/utils/conversion.hpp"
#include "slangd/utils/source_utils.hpp"

namespace slangd {

// Helper function to unwrap TransparentMember symbols
auto GetUnwrappedSymbol(const slang::ast::Symbol& symbol)
    -> const slang::ast::Symbol& {
  using SK = slang::ast::SymbolKind;

  // If not a TransparentMember, return directly
  if (symbol.kind != SK::TransparentMember) {
    return symbol;
  }

  // Recursively unwrap
  return GetUnwrappedSymbol(
      symbol.as<slang::ast::TransparentMemberSymbol>().wrapped);
}

auto ShouldIncludeSymbol(
    const slang::ast::Symbol& symbol,
    const std::shared_ptr<slang::SourceManager>& source_manager,
    const std::string& uri) -> bool {
  using SK = slang::ast::SymbolKind;

  // Skip symbols without a valid location
  if (!symbol.location) {
    return false;
  }

  // Skip unnamed symbols
  if (symbol.name.empty()) {
    return false;
  }

  // Skip symbols that are compiler-generated (no source location)
  if (source_manager->isPreprocessedLoc(symbol.location)) {
    return false;
  }

  // Check if the symbol is from the current document using our utility
  if (!IsLocationInDocument(symbol.location, source_manager, uri)) {
    return false;
  }

  // Include only specific types of symbols that are relevant to users
  switch (symbol.kind) {
    // Top-level design elements
    case SK::Package:
    case SK::Definition:
      return true;

    // Types
    case SK::TypeAlias:
    case SK::EnumType:
    case SK::PackedStructType:
    case SK::UnpackedStructType:
    case SK::PackedUnionType:
    case SK::UnpackedUnionType:
    case SK::ClassType:
      return true;

    // Functions and Tasks
    case SK::Subroutine:
      return true;

    // Important declarations
    case SK::Parameter:
    case SK::TypeParameter:
    case SK::Modport:
      return true;

    // For variables, only include ports and parameters
    case SK::Port:
    case SK::Variable:
    case SK::Net:
    case SK::Instance:
      return true;

    // For enum values, include them
    case SK::EnumValue:
      return true;

    // For struct fields, include them
    case SK::Field:
      return true;

    // For uninstantiated definitions, include them
    case SK::UninstantiatedDef:
      return true;

    // Default: exclude other symbols
    default:
      return false;
  }
}

// Maps a Slang symbol to an LSP symbol kind
auto MapSymbolToLspSymbolKind(const slang::ast::Symbol& symbol)
    -> lsp::SymbolKind {
  using SK = slang::ast::SymbolKind;
  using LK = lsp::SymbolKind;
  using DK = slang::ast::DefinitionKind;

  // if is type alias, need to get the canonical type and use that to determine
  // the symbol kind
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
    case SK::EnumValue:
      return LK::kConstant;

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

auto GetSymbolNameLocationRange(
    const slang::ast::Symbol& symbol,
    const std::shared_ptr<slang::SourceManager>& source_manager) -> lsp::Range {
  // Just use the symbol's declaration location
  return ConvertSlangLocationToLspRange(symbol.location, source_manager);
}

void ProcessScopeMembers(
    const slang::ast::Scope& scope, lsp::DocumentSymbol& parent_symbol,
    const std::shared_ptr<slang::SourceManager>& source_manager,
    const std::string& uri, slang::ast::Compilation& compilation) {
  // Each scope gets its own set of seen names
  std::unordered_set<std::string> scope_seen_names;

  for (const auto& member : scope.members()) {
    // Unwrap at the boundary before passing to BuildDocumentSymbolHierarchy
    const auto& unwrapped_member = GetUnwrappedSymbol(member);

    std::vector<lsp::DocumentSymbol> child_symbols;
    BuildDocumentSymbolHierarchy(
        unwrapped_member, child_symbols, source_manager, uri, scope_seen_names,
        compilation);
    for (auto& child : child_symbols) {
      parent_symbol.children->push_back(std::move(child));
    }
  }
}

// Process the children of a symbol with type-specific traversal logic
void ProcessSymbolChildren(
    const slang::ast::Symbol& symbol, lsp::DocumentSymbol& parent_symbol,
    const std::shared_ptr<slang::SourceManager>& source_manager,
    const std::string& uri, slang::ast::Compilation& compilation) {
  using SK = slang::ast::SymbolKind;

  // Handle different symbol types
  if (symbol.kind == SK::Package) {
    // Packages need special handling to reach their members
    const auto& package = symbol.as<slang::ast::PackageSymbol>();
    ProcessScopeMembers(
        package, parent_symbol, source_manager, uri, compilation);
  } else if (symbol.kind == SK::Definition) {
    const auto& definition_symbol = symbol.as<slang::ast::DefinitionSymbol>();
    auto& inst_symbol = slang::ast::InstanceSymbol::createDefault(
        compilation, definition_symbol);
    const auto& body = inst_symbol.body;
    if (body.isScope()) {
      const auto& scope = body.as<slang::ast::Scope>();
      ProcessScopeMembers(
          scope, parent_symbol, source_manager, uri, compilation);
    }
  } else if (symbol.kind == SK::TypeAlias) {
    // Type aliases need to be unwrapped to process their members
    const auto& type_alias = symbol.as<slang::ast::TypeAliasType>();
    const auto& canonical_type = type_alias.getCanonicalType();

    if (canonical_type.kind == SK::PackedStructType) {
      const auto& struct_type =
          canonical_type.as<slang::ast::PackedStructType>();
      // Make sure to process all members of the struct
      ProcessScopeMembers(
          struct_type, parent_symbol, source_manager, uri, compilation);
    } else if (canonical_type.kind == SK::UnpackedStructType) {
      const auto& struct_type =
          canonical_type.as<slang::ast::UnpackedStructType>();
      ProcessScopeMembers(
          struct_type, parent_symbol, source_manager, uri, compilation);
    } else if (canonical_type.kind == SK::PackedUnionType) {
      const auto& union_type = canonical_type.as<slang::ast::PackedUnionType>();
      ProcessScopeMembers(
          union_type, parent_symbol, source_manager, uri, compilation);
    } else if (canonical_type.kind == SK::UnpackedUnionType) {
      const auto& union_type =
          canonical_type.as<slang::ast::UnpackedUnionType>();
      ProcessScopeMembers(
          union_type, parent_symbol, source_manager, uri, compilation);
    }
  }
  // else if kind is "field" need to check the type. if is struct or union, we
  // n=might have nested fields!
  else if (symbol.kind == SK::Field) {
    const auto& field = symbol.as<slang::ast::FieldSymbol>();
    const auto& type = field.getType();
    if (type.kind == SK::PackedStructType) {
      const auto& struct_type = type.as<slang::ast::PackedStructType>();
      ProcessScopeMembers(
          struct_type, parent_symbol, source_manager, uri, compilation);
    } else if (type.kind == SK::UnpackedStructType) {
      const auto& struct_type = type.as<slang::ast::UnpackedStructType>();
      ProcessScopeMembers(
          struct_type, parent_symbol, source_manager, uri, compilation);
    } else if (type.kind == SK::PackedUnionType) {
      const auto& union_type = type.as<slang::ast::PackedUnionType>();
      ProcessScopeMembers(
          union_type, parent_symbol, source_manager, uri, compilation);
    } else if (type.kind == SK::UnpackedUnionType) {
      const auto& union_type = type.as<slang::ast::UnpackedUnionType>();
      ProcessScopeMembers(
          union_type, parent_symbol, source_manager, uri, compilation);
    }
  }
  // For all other symbol types, don't traverse
}

void BuildDocumentSymbolHierarchy(
    const slang::ast::Symbol& symbol,
    std::vector<lsp::DocumentSymbol>& document_symbols,
    const std::shared_ptr<slang::SourceManager>& source_manager,
    const std::string& uri, std::unordered_set<std::string>& seen_names,
    slang::ast::Compilation& compilation) {
  // Only include symbols from the current document and with relevant kinds
  if (!ShouldIncludeSymbol(symbol, source_manager, uri)) {
    return;
  }

  // Skip if we've seen this name already in current scope
  if (seen_names.find(std::string(symbol.name)) != seen_names.end()) {
    return;
  }

  // Create a document symbol for this symbol
  lsp::DocumentSymbol doc_symbol;
  doc_symbol.name = std::string(symbol.name);

  // Get the symbol kind from our mapping function
  doc_symbol.kind = MapSymbolToLspSymbolKind(symbol);

  // Get the symbol's location range
  if (symbol.location) {
    doc_symbol.range =
        ConvertSlangLocationToLspRange(symbol.location, source_manager);
    doc_symbol.selectionRange =
        GetSymbolNameLocationRange(symbol, source_manager);
  }

  // Add this symbol name to seen names in current scope to prevent duplicates
  seen_names.insert(doc_symbol.name);

  // Process children with type-specific traversal logic
  doc_symbol.children = std::vector<lsp::DocumentSymbol>();
  ProcessSymbolChildren(symbol, doc_symbol, source_manager, uri, compilation);

  // Only add this symbol if it has a valid range or has children
  if (symbol.location || doc_symbol.children->empty()) {
    document_symbols.push_back(std::move(doc_symbol));
  }
}

auto GetDocumentSymbols(
    slang::ast::Compilation& compilation,
    const std::shared_ptr<slang::SourceManager>& source_manager,
    const std::string& uri) -> std::vector<lsp::DocumentSymbol> {
  std::vector<lsp::DocumentSymbol> result;
  std::unordered_set<std::string> seen_names;

  // Get top-level definitions (modules, interfaces, etc.)
  for (const auto& def : compilation.getDefinitions()) {
    BuildDocumentSymbolHierarchy(
        GetUnwrappedSymbol(*def), result, source_manager, uri, seen_names,
        compilation);
  }

  // Get packages
  for (const auto& package : compilation.getPackages()) {
    BuildDocumentSymbolHierarchy(
        GetUnwrappedSymbol(*package), result, source_manager, uri, seen_names,
        compilation);
  }

  return result;
}

}  // namespace slangd

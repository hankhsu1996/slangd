#include "slangd/features/symbols_provider.hpp"

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

auto SymbolsProvider::GetSymbolsForUri(const std::string& uri)
    -> std::vector<lsp::DocumentSymbol> {
  // Get the compilation for this document
  auto comp_it = document_manager_->GetCompilation(uri);
  auto sm_it = document_manager_->GetSourceManager(uri);

  // If either compilation or source manager is missing, return empty vector
  if (!comp_it || !sm_it) {
    return {};
  }

  // Use the implementation that takes a compilation and source manager
  return ResolveSymbolsFromCompilation(*comp_it, sm_it, uri);
}

auto SymbolsProvider::ResolveSymbolsFromCompilation(
    slang::ast::Compilation& compilation,
    const std::shared_ptr<slang::SourceManager>& source_manager,
    const std::string& uri) -> std::vector<lsp::DocumentSymbol> {
  std::vector<lsp::DocumentSymbol> result;
  std::unordered_set<std::string> seen_names;

  // Get top-level definitions (modules, interfaces, etc.)
  for (const auto& def : compilation.getDefinitions()) {
    BuildSymbolHierarchy(
        GetUnwrappedSymbol(*def), result, source_manager, uri, seen_names,
        compilation);
  }

  // Get packages
  for (const auto& package : compilation.getPackages()) {
    BuildSymbolHierarchy(
        GetUnwrappedSymbol(*package), result, source_manager, uri, seen_names,
        compilation);
  }

  // Log the number of symbols found
  logger_->debug(
      "SymbolsProvider found {} document symbols for document: {}",
      result.size(), uri);

  return result;
}

void SymbolsProvider::BuildSymbolHierarchy(
    const slang::ast::Symbol& symbol,
    std::vector<lsp::DocumentSymbol>& document_symbols,
    const std::shared_ptr<slang::SourceManager>& source_manager,
    const std::string& uri, std::unordered_set<std::string>& seen_names,
    slang::ast::Compilation& compilation) {
  // Only include symbols from the current document and with relevant kinds
  if (!IsSymbolInUriDocument(symbol, source_manager, uri)) {
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
  doc_symbol.kind = ConvertSymbolKindToLsp(symbol);

  // Get the symbol's location range
  if (symbol.location) {
    doc_symbol.range =
        ConvertSlangLocationToLspRange(symbol.location, source_manager);
    doc_symbol.selectionRange =
        ConvertSymbolNameRangeToLsp(symbol, source_manager);
  }

  // Add this symbol name to seen names in current scope to prevent duplicates
  seen_names.insert(doc_symbol.name);

  // Process children with type-specific traversal logic
  doc_symbol.children = std::vector<lsp::DocumentSymbol>();
  BuildSymbolChildren(symbol, doc_symbol, source_manager, uri, compilation);

  // Only add this symbol if it has a valid range or has children
  if (symbol.location || doc_symbol.children->empty()) {
    document_symbols.push_back(std::move(doc_symbol));
  }
}

void SymbolsProvider::BuildSymbolChildren(
    const slang::ast::Symbol& symbol, lsp::DocumentSymbol& parent_symbol,
    const std::shared_ptr<slang::SourceManager>& source_manager,
    const std::string& uri, slang::ast::Compilation& compilation) {
  using SK = slang::ast::SymbolKind;

  // Handle different symbol types
  if (symbol.kind == SK::Package) {
    // Packages need special handling to reach their members
    const auto& package = symbol.as<slang::ast::PackageSymbol>();
    BuildScopeSymbolChildren(
        package, parent_symbol, source_manager, uri, compilation);
  } else if (symbol.kind == SK::Definition) {
    const auto& definition_symbol = symbol.as<slang::ast::DefinitionSymbol>();
    auto& inst_symbol = slang::ast::InstanceSymbol::createDefault(
        compilation, definition_symbol);
    const auto& body = inst_symbol.body;
    if (body.isScope()) {
      const auto& scope = body.as<slang::ast::Scope>();
      BuildScopeSymbolChildren(
          scope, parent_symbol, source_manager, uri, compilation);
    }
  } else if (symbol.kind == SK::TypeAlias) {
    // Type aliases need to be unwrapped to process their members
    const auto& type_alias = symbol.as<slang::ast::TypeAliasType>();
    const auto& canonical_type = type_alias.getCanonicalType();

    if (canonical_type.kind == SK::EnumType) {
      // Special handling for enums - add enum values as children
      const auto& enum_type = canonical_type.as<slang::ast::EnumType>();

      // Create a separate scope for enum values to avoid name conflicts
      std::unordered_set<std::string> enum_seen_names;

      // Process all enum values from the enum type
      for (const auto& enum_value : enum_type.values()) {
        // Only check if the enum value is in the current document
        // (we only check location, not symbol kind since we know it's an enum
        // value)
        if (IsSymbolInDocument(enum_value, source_manager, uri)) {
          // Create a document symbol for this enum value
          lsp::DocumentSymbol enum_value_symbol;
          enum_value_symbol.name = std::string(enum_value.name);
          enum_value_symbol.kind = lsp::SymbolKind::kEnumMember;

          if (enum_value.location) {
            enum_value_symbol.range = ConvertSlangLocationToLspRange(
                enum_value.location, source_manager);
            enum_value_symbol.selectionRange =
                ConvertSymbolNameRangeToLsp(enum_value, source_manager);
          }

          // Add empty children vector
          enum_value_symbol.children = std::vector<lsp::DocumentSymbol>();

          // Add to parent's children
          parent_symbol.children->push_back(std::move(enum_value_symbol));
        }
      }
    } else if (canonical_type.kind == SK::PackedStructType) {
      const auto& struct_type =
          canonical_type.as<slang::ast::PackedStructType>();
      // Make sure to process all members of the struct
      BuildScopeSymbolChildren(
          struct_type, parent_symbol, source_manager, uri, compilation);
    } else if (canonical_type.kind == SK::UnpackedStructType) {
      const auto& struct_type =
          canonical_type.as<slang::ast::UnpackedStructType>();
      BuildScopeSymbolChildren(
          struct_type, parent_symbol, source_manager, uri, compilation);
    } else if (canonical_type.kind == SK::PackedUnionType) {
      const auto& union_type = canonical_type.as<slang::ast::PackedUnionType>();
      BuildScopeSymbolChildren(
          union_type, parent_symbol, source_manager, uri, compilation);
    } else if (canonical_type.kind == SK::UnpackedUnionType) {
      const auto& union_type =
          canonical_type.as<slang::ast::UnpackedUnionType>();
      BuildScopeSymbolChildren(
          union_type, parent_symbol, source_manager, uri, compilation);
    }
  }
  // Process nested fields within structs and unions
  else if (symbol.kind == SK::Field) {
    const auto& field = symbol.as<slang::ast::FieldSymbol>();
    const auto& type = field.getType();
    if (type.kind == SK::PackedStructType) {
      const auto& struct_type = type.as<slang::ast::PackedStructType>();
      BuildScopeSymbolChildren(
          struct_type, parent_symbol, source_manager, uri, compilation);
    } else if (type.kind == SK::UnpackedStructType) {
      const auto& struct_type = type.as<slang::ast::UnpackedStructType>();
      BuildScopeSymbolChildren(
          struct_type, parent_symbol, source_manager, uri, compilation);
    } else if (type.kind == SK::PackedUnionType) {
      const auto& union_type = type.as<slang::ast::PackedUnionType>();
      BuildScopeSymbolChildren(
          union_type, parent_symbol, source_manager, uri, compilation);
    } else if (type.kind == SK::UnpackedUnionType) {
      const auto& union_type = type.as<slang::ast::UnpackedUnionType>();
      BuildScopeSymbolChildren(
          union_type, parent_symbol, source_manager, uri, compilation);
    }
  }
  // For all other symbol types, don't traverse
}

void SymbolsProvider::BuildScopeSymbolChildren(
    const slang::ast::Scope& scope, lsp::DocumentSymbol& parent_symbol,
    const std::shared_ptr<slang::SourceManager>& source_manager,
    const std::string& uri, slang::ast::Compilation& compilation) {
  // Each scope gets its own set of seen names
  std::unordered_set<std::string> scope_seen_names;

  // Process all members (enum values are filtered out by IsSymbolInUriDocument)
  for (const auto& member : scope.members()) {
    // Unwrap at the boundary before passing to BuildSymbolHierarchy
    const auto& unwrapped_member = GetUnwrappedSymbol(member);

    std::vector<lsp::DocumentSymbol> child_symbols;
    BuildSymbolHierarchy(
        unwrapped_member, child_symbols, source_manager, uri, scope_seen_names,
        compilation);
    for (auto& child : child_symbols) {
      parent_symbol.children->push_back(std::move(child));
    }
  }
}

auto SymbolsProvider::ConvertSymbolKindToLsp(const slang::ast::Symbol& symbol)
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

auto SymbolsProvider::ConvertSymbolNameRangeToLsp(
    const slang::ast::Symbol& symbol,
    const std::shared_ptr<slang::SourceManager>& source_manager) -> lsp::Range {
  // Just use the symbol's declaration location
  return ConvertSlangLocationToLspRange(symbol.location, source_manager);
}

auto SymbolsProvider::IsSymbolInDocument(
    const slang::ast::Symbol& symbol,
    const std::shared_ptr<slang::SourceManager>& source_manager,
    const std::string& uri) -> bool {
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
  return IsLocationInDocument(symbol.location, source_manager, uri);
}

auto SymbolsProvider::IsRelevantDocumentSymbol(const slang::ast::Symbol& symbol)
    -> bool {
  using SK = slang::ast::SymbolKind;

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

    // For struct fields, include them
    case SK::Field:
      return true;

    // For uninstantiated definitions, include them
    case SK::UninstantiatedDef:
      return true;

    // Explicitly exclude enum values - they'll be handled through their parent
    // enum types
    case SK::EnumValue:
      return false;

    // Default: exclude other symbols
    default:
      return false;
  }
}

auto SymbolsProvider::IsSymbolInUriDocument(
    const slang::ast::Symbol& symbol,
    const std::shared_ptr<slang::SourceManager>& source_manager,
    const std::string& uri) -> bool {
  // Combine both checks
  return IsSymbolInDocument(symbol, source_manager, uri) &&
         IsRelevantDocumentSymbol(symbol);
}

// Helper function to unwrap TransparentMember symbols
auto SymbolsProvider::GetUnwrappedSymbol(const slang::ast::Symbol& symbol)
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

}  // namespace slangd

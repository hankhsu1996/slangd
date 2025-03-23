#include "include/slangd/symbol_utils.hpp"

#include <unordered_set>

#include "slang/ast/Scope.h"
#include "slang/ast/Symbol.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/MemberSymbols.h"
#include "slang/ast/symbols/ParameterSymbols.h"
#include "slang/ast/symbols/VariableSymbols.h"
#include "slang/ast/types/AllTypes.h"
#include "slang/ast/types/Type.h"
#include "slang/text/SourceManager.h"
#include "spdlog/spdlog.h"

namespace slangd {

// Helper function to unwrap TransparentMember symbols
const slang::ast::Symbol& GetUnwrappedSymbol(const slang::ast::Symbol& symbol) {
  using SK = slang::ast::SymbolKind;

  // If not a TransparentMember, return directly
  if (symbol.kind != SK::TransparentMember) {
    return symbol;
  }

  // Recursively unwrap
  return GetUnwrappedSymbol(
      symbol.as<slang::ast::TransparentMemberSymbol>().wrapped);
}

bool ShouldIncludeSymbol(
    const slang::ast::Symbol& symbol,
    const std::shared_ptr<slang::SourceManager>& source_manager,
    const std::string& uri) {
  using SK = slang::ast::SymbolKind;

  // add kind of symbol
  spdlog::debug(
      "Checking if symbol {} of kind {} should be included", symbol.name,
      slang::ast::toString(symbol.kind));

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

  // Check if the symbol is from the current document
  auto full_source_path =
      std::string(source_manager->getFileName(symbol.location));

  // Extract just the filename part
  std::string source_file_path = full_source_path;
  size_t last_slash = full_source_path.find_last_of("/\\");
  if (last_slash != std::string::npos) {
    source_file_path = full_source_path.substr(last_slash + 1);
  }

  // Extract filename from URI too
  std::string uri_filename = uri;
  size_t uri_last_slash = uri.find_last_of("/\\");
  if (uri_last_slash != std::string::npos) {
    uri_filename = uri.substr(uri_last_slash + 1);
  }

  // Just match based on filenames for now
  bool is_from_current_doc = (source_file_path == uri_filename);
  if (!is_from_current_doc) {
    return false;
  }

  // Include only specific types of symbols that are relevant to users
  switch (symbol.kind) {
    // Top-level design elements
    case SK::Instance:
    case SK::InstanceBody:
    case SK::InstanceArray:
    case SK::Package:
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
      return true;

    // For enum values, include them
    case SK::EnumValue:
      return true;

    // For struct fields, include them
    case SK::Field:
      return true;

    // Default: exclude other symbols
    default:
      return false;
  }
}

// Maps a Slang symbol to an LSP symbol kind
lsp::SymbolKind MapSymbolToLspSymbolKind(const slang::ast::Symbol& symbol) {
  using SK = slang::ast::SymbolKind;
  using LK = lsp::SymbolKind;

  if (symbol.kind == SK::Instance) {
    auto& instance = symbol.as<slang::ast::InstanceSymbol>();
    if (instance.isModule()) {
      return LK::Class;
    } else if (instance.isInterface()) {
      return LK::Interface;
    } else {
      return LK::Object;
    }
  }

  // if is type alias, need to get the canonical type and use that to determine
  // the symbol kind
  if (symbol.kind == SK::TypeAlias) {
    auto& type_alias = symbol.as<slang::ast::TypeAliasType>();
    auto& canonical_type = type_alias.getCanonicalType();

    switch (canonical_type.kind) {
      case SK::EnumType:
        return LK::Enum;
      case SK::PackedStructType:
      case SK::UnpackedStructType:
      case SK::PackedUnionType:
      case SK::UnpackedUnionType:
        return LK::Struct;
      default:
        return LK::TypeParameter;
    }
  }

  // For non-Definition symbols, use a switch on the symbol kind
  switch (symbol.kind) {
    // Module-related
    case SK::Instance:
    case SK::InstanceBody:
    case SK::InstanceArray:
      return LK::Class;

    // Package
    case SK::Package:
      return LK::Package;

    // Variables and data
    case SK::Variable:
    case SK::Net:
    case SK::Port:
      return LK::Variable;

    case SK::Field:
    case SK::ClassProperty:
      return LK::Field;

    case SK::Parameter:
    case SK::EnumValue:
      return LK::Constant;

    case SK::TypeParameter:
      return LK::TypeParameter;

    // Type-related
    case SK::TypeAlias:
    case SK::ForwardingTypedef:
      return LK::TypeParameter;

    case SK::EnumType:
      return LK::Enum;

    case SK::PackedStructType:
    case SK::UnpackedStructType:
      return LK::Struct;

    case SK::PackedUnionType:
    case SK::UnpackedUnionType:
      return LK::Class;

    case SK::ClassType:
      return LK::Class;

    // Interface-related
    case SK::Modport:
      return LK::Interface;

    // Function-related
    case SK::Subroutine:
      return LK::Function;

    // Default for other symbol kinds
    default:
      return LK::Object;
  }
}

lsp::Range ConvertSlangLocationToLspRange(
    const slang::SourceLocation& location,
    const std::shared_ptr<slang::SourceManager>& source_manager) {
  if (!location) return lsp::Range{};

  auto line = source_manager->getLineNumber(location);
  auto column = source_manager->getColumnNumber(location);

  // Create a range that spans a single character at the location
  lsp::Position start_pos{
      static_cast<int>(line - 1),   // Convert to 0-based
      static_cast<int>(column - 1)  // Convert to 0-based
  };

  // For now, we set end position same as start for a single point range
  return lsp::Range{start_pos, start_pos};
}

lsp::Range GetSymbolNameLocationRange(
    const slang::ast::Symbol& symbol,
    const std::shared_ptr<slang::SourceManager>& source_manager) {
  // Just use the symbol's declaration location
  return ConvertSlangLocationToLspRange(symbol.location, source_manager);
}

void ProcessScopeMembers(
    const slang::ast::Scope& scope, lsp::DocumentSymbol& parent_symbol,
    const std::shared_ptr<slang::SourceManager>& source_manager,
    const std::string& uri) {
  // Each scope gets its own set of seen names
  std::unordered_set<std::string> scope_seen_names;

  for (const auto& member : scope.members()) {
    // Unwrap at the boundary before passing to BuildDocumentSymbolHierarchy
    const auto& unwrapped_member = GetUnwrappedSymbol(member);

    std::vector<lsp::DocumentSymbol> child_symbols;
    BuildDocumentSymbolHierarchy(
        unwrapped_member, child_symbols, source_manager, uri, scope_seen_names);
    for (auto& child : child_symbols) {
      parent_symbol.children.push_back(std::move(child));
    }
  }
}

// Process the children of a symbol with type-specific traversal logic
void ProcessSymbolChildren(
    const slang::ast::Symbol& symbol, lsp::DocumentSymbol& parent_symbol,
    const std::shared_ptr<slang::SourceManager>& source_manager,
    const std::string& uri) {
  using SK = slang::ast::SymbolKind;

  // Handle different symbol types
  if (symbol.kind == SK::Package) {
    // Packages need special handling to reach their members
    const auto& package = symbol.as<slang::ast::PackageSymbol>();
    ProcessScopeMembers(package, parent_symbol, source_manager, uri);
  } else if (symbol.kind == SK::Instance) {
    // Instances need special handling to reach their body
    const auto& instance = symbol.as<slang::ast::InstanceSymbol>();
    const auto& body = instance.body;
    if (body.isScope()) {
      const auto& scope = body.as<slang::ast::Scope>();
      ProcessScopeMembers(scope, parent_symbol, source_manager, uri);
    }
  } else if (symbol.kind == SK::TypeAlias) {
    // Type aliases need to be unwrapped to process their members
    auto& typeAlias = symbol.as<slang::ast::TypeAliasType>();
    auto& canonicalType = typeAlias.getCanonicalType();

    if (canonicalType.kind == SK::PackedStructType) {
      auto& structType = canonicalType.as<slang::ast::PackedStructType>();
      // Make sure to process all members of the struct
      ProcessScopeMembers(structType, parent_symbol, source_manager, uri);
    } else if (canonicalType.kind == SK::UnpackedStructType) {
      auto& structType = canonicalType.as<slang::ast::UnpackedStructType>();
      ProcessScopeMembers(structType, parent_symbol, source_manager, uri);
    } else if (canonicalType.kind == SK::PackedUnionType) {
      auto& unionType = canonicalType.as<slang::ast::PackedUnionType>();
      ProcessScopeMembers(unionType, parent_symbol, source_manager, uri);
    } else if (canonicalType.kind == SK::UnpackedUnionType) {
      auto& unionType = canonicalType.as<slang::ast::UnpackedUnionType>();
      ProcessScopeMembers(unionType, parent_symbol, source_manager, uri);
    }
  }
  // else if kind is "field" need to check the type. if is struct or union, we
  // n=might have nested fields!
  else if (symbol.kind == SK::Field) {
    auto& field = symbol.as<slang::ast::FieldSymbol>();
    auto& type = field.getType();
    if (type.kind == SK::PackedStructType) {
      auto& structType = type.as<slang::ast::PackedStructType>();
      ProcessScopeMembers(structType, parent_symbol, source_manager, uri);
    } else if (type.kind == SK::UnpackedStructType) {
      auto& structType = type.as<slang::ast::UnpackedStructType>();
      ProcessScopeMembers(structType, parent_symbol, source_manager, uri);
    } else if (type.kind == SK::PackedUnionType) {
      auto& unionType = type.as<slang::ast::PackedUnionType>();
      ProcessScopeMembers(unionType, parent_symbol, source_manager, uri);
    } else if (type.kind == SK::UnpackedUnionType) {
      auto& unionType = type.as<slang::ast::UnpackedUnionType>();
      ProcessScopeMembers(unionType, parent_symbol, source_manager, uri);
    }
  }
  // For all other symbol types, don't traverse
}

void BuildDocumentSymbolHierarchy(
    const slang::ast::Symbol& symbol,
    std::vector<lsp::DocumentSymbol>& document_symbols,
    const std::shared_ptr<slang::SourceManager>& source_manager,
    const std::string& uri, std::unordered_set<std::string>& seen_names) {
  // Now symbol is expected to be already unwrapped at the entry point

  // spdlog the symbol name, kind, location (file:line:col)
  spdlog::debug(
      "Symbol: name={} kind={} location={}:{}:{} is_scope={}", symbol.name,
      slang::ast::toString(symbol.kind),
      source_manager->getFileName(symbol.location),
      source_manager->getLineNumber(symbol.location),
      source_manager->getColumnNumber(symbol.location), symbol.isScope());

  // Only include symbols from the current document and with relevant kinds
  if (!ShouldIncludeSymbol(symbol, source_manager, uri)) return;

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
  ProcessSymbolChildren(symbol, doc_symbol, source_manager, uri);

  // Only add this symbol if it has a valid range or has children
  if (symbol.location || !doc_symbol.children.empty()) {
    document_symbols.push_back(std::move(doc_symbol));
  }
}

std::vector<lsp::DocumentSymbol> GetDocumentSymbols(
    slang::ast::Compilation& compilation,
    const std::shared_ptr<slang::SourceManager>& source_manager,
    const std::string& uri) {
  std::vector<lsp::DocumentSymbol> result;
  std::unordered_set<std::string> seen_names;

  // Get top-level definitions (modules, interfaces, etc.)
  for (const auto& def : compilation.getDefinitions()) {
    auto& definition_symbol = def->as<slang::ast::DefinitionSymbol>();
    auto& inst_symbol = slang::ast::InstanceSymbol::createDefault(
        compilation, definition_symbol);

    // Unwrap at the boundary before passing to BuildDocumentSymbolHierarchy
    BuildDocumentSymbolHierarchy(
        GetUnwrappedSymbol(inst_symbol), result, source_manager, uri,
        seen_names);
  }

  // Get packages
  for (const auto& package : compilation.getPackages()) {
    // Unwrap at the boundary before passing to BuildDocumentSymbolHierarchy
    BuildDocumentSymbolHierarchy(
        GetUnwrappedSymbol(*package), result, source_manager, uri, seen_names);
  }

  return result;
}

}  // namespace slangd

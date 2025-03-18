#include "include/slangd/symbol_utils.hpp"

#include <unordered_set>

#include "slang/ast/Scope.h"
#include "slang/ast/Symbol.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/ParameterSymbols.h"
#include "slang/ast/types/Type.h"
#include "slang/text/SourceManager.h"

namespace slangd {

// Maps a Slang symbol to an LSP symbol kind
lsp::SymbolKind MapSymbolToLspSymbolKind(const slang::ast::Symbol& symbol) {
  using SK = slang::ast::SymbolKind;
  using LK = lsp::SymbolKind;

  if (symbol.kind == SK::Instance) {
    try {
      auto& instance = symbol.as<slang::ast::InstanceSymbol>();
      if (instance.isModule()) {
        return LK::Module;
      } else if (instance.isInterface()) {
        return LK::Interface;
      }
    } catch (...) {
      // If casting failed, default to Object
      return LK::Object;
    }
  }

  // For non-Definition symbols, use a switch on the symbol kind
  switch (symbol.kind) {
    // Module-related
    case SK::Instance:
    case SK::InstanceBody:
    case SK::InstanceArray:
      return LK::Module;

    // Package
    case SK::Package:
      return LK::Package;

    // Variables and data
    case SK::Variable:
    case SK::Net:
    case SK::Port:
    case SK::Field:
    case SK::ClassProperty:
      return LK::Variable;

    case SK::Parameter:
    case SK::EnumValue:
      return LK::Constant;

    // Type-related
    case SK::TypeAlias:
    case SK::TypeParameter:
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

bool ShouldIncludeSymbol(
    const slang::ast::Symbol& symbol,
    const std::shared_ptr<slang::SourceManager>& source_manager,
    const std::string& uri) {
  // Skip symbols without a valid location
  if (!symbol.location) {
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

  return is_from_current_doc;
}

void ProcessScopeMembers(
    const slang::ast::Scope& scope, lsp::DocumentSymbol& parent_symbol,
    const std::shared_ptr<slang::SourceManager>& source_manager,
    const std::string& uri) {
  // Each scope gets its own set of seen names
  std::unordered_set<std::string> scope_seen_names;

  for (const auto& member : scope.members()) {
    std::vector<lsp::DocumentSymbol> child_symbols;
    BuildDocumentSymbolHierarchy(
        member, child_symbols, source_manager, uri, scope_seen_names);
    for (auto& child : child_symbols) {
      parent_symbol.children.push_back(std::move(child));
    }
  }
}

void BuildDocumentSymbolHierarchy(
    const slang::ast::Symbol& symbol,
    std::vector<lsp::DocumentSymbol>& document_symbols,
    const std::shared_ptr<slang::SourceManager>& source_manager,
    const std::string& uri, std::unordered_set<std::string>& seen_names) {
  // Only include symbols from the current document
  if (!ShouldIncludeSymbol(symbol, source_manager, uri)) return;

  // Skip if we've seen this name already in current scope
  if (!symbol.name.empty() &&
      seen_names.find(std::string(symbol.name)) != seen_names.end()) {
    return;
  }

  // Create a document symbol for this symbol
  lsp::DocumentSymbol doc_symbol;
  doc_symbol.name = std::string(symbol.name);

  // If the symbol has no name, use a default based on kind
  if (doc_symbol.name.empty()) {
    doc_symbol.name =
        "<unnamed " + std::string(slang::ast::toString(symbol.kind)) + ">";
  }

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
  if (!doc_symbol.name.empty()) {
    seen_names.insert(doc_symbol.name);
  }

  // Process children - handle different kinds of scopes
  if (symbol.isScope()) {
    // Direct scope
    const auto& scope = symbol.as<slang::ast::Scope>();
    ProcessScopeMembers(scope, doc_symbol, source_manager, uri);
  } else if (symbol.kind == slang::ast::SymbolKind::Instance) {
    // Instance symbol - need to use its body
    const auto& instance = symbol.as<slang::ast::InstanceSymbol>();
    const auto& body = instance.body;

    if (body.isScope()) {
      const auto& scope = body.as<slang::ast::Scope>();
      ProcessScopeMembers(scope, doc_symbol, source_manager, uri);
    }
  }

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
    BuildDocumentSymbolHierarchy(
        inst_symbol, result, source_manager, uri, seen_names);
  }

  // Get packages
  for (const auto& package : compilation.getPackages()) {
    BuildDocumentSymbolHierarchy(
        *package, result, source_manager, uri, seen_names);
  }

  return result;
}

}  // namespace slangd

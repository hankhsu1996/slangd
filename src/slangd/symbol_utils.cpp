#include "include/slangd/symbol_utils.hpp"

#include <iostream>

#include "slang/ast/Scope.h"
#include "slang/ast/Symbol.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/types/Type.h"
#include "slang/text/SourceManager.h"

namespace slangd {

// Maps a Slang symbol to an LSP symbol kind
lsp::SymbolKind MapSymbolToLspSymbolKind(const slang::ast::Symbol& symbol) {
  using SK = slang::ast::SymbolKind;
  using LK = lsp::SymbolKind;

  std::cout << "Mapping symbol '" << symbol.name << "' to LSP kind"
            << std::endl;

  std::cout << "Symbol kind: " << slang::ast::toString(symbol.kind)
            << std::endl;

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

  std::cout << "Symbol '" << symbol.name << "' source path: '"
            << source_file_path << "', URI filename: '" << uri_filename << "'"
            << std::endl;

  // Just match based on filenames for now
  bool is_from_current_doc = (source_file_path == uri_filename);

  if (!is_from_current_doc) {
    std::cout << "Skipping symbol '" << symbol.name
              << "' from different file: " << source_file_path << std::endl;
  }

  return is_from_current_doc;
}

void BuildDocumentSymbolHierarchy(
    const slang::ast::Symbol& symbol,
    std::vector<lsp::DocumentSymbol>& document_symbols,
    const std::shared_ptr<slang::SourceManager>& source_manager,
    const std::string& uri) {
  // Only include symbols from the current document
  if (!ShouldIncludeSymbol(symbol, source_manager, uri)) return;

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

  //   print the current symbol name, print if this is a scope
  std::cout << "Processing symbol: " << symbol.name
            << " (scope: " << (symbol.isScope() ? "true" : "false") << ")"
            << std::endl;

  // Process children for scope symbols (but not instances)
  if (symbol.isScope() && symbol.kind != slang::ast::SymbolKind::Instance) {
    const auto& scope = symbol.as<slang::ast::Scope>();

    // Process regular members
    for (const auto& member : scope.members()) {
      // Recursively process child symbols
      std::vector<lsp::DocumentSymbol> child_symbols;
      BuildDocumentSymbolHierarchy(member, child_symbols, source_manager, uri);

      // Add non-empty child symbols to this symbol's children
      if (!child_symbols.empty()) {
        // Add children to the children vector
        for (auto& child : child_symbols) {
          doc_symbol.children.push_back(std::move(child));
        }
      }
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

  // Get top-level definitions (modules, interfaces, etc.)
  for (const auto& def : compilation.getDefinitions()) {
    auto& definition_symbol = def->as<slang::ast::DefinitionSymbol>();
    auto& inst_symbol = slang::ast::InstanceSymbol::createDefault(
        compilation, definition_symbol);
    BuildDocumentSymbolHierarchy(inst_symbol, result, source_manager, uri);
  }

  // Get packages
  for (const auto& package : compilation.getPackages()) {
    BuildDocumentSymbolHierarchy(*package, result, source_manager, uri);
  }

  // NOTE: We don't have direct access to the compilation's root symbol
  // Use only definitions and packages for now, which should cover most cases

  return result;
}

}  // namespace slangd

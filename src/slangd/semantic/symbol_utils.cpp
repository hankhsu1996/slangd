#include "slangd/semantic/symbol_utils.hpp"

#include <slang/ast/Scope.h>
#include <slang/ast/symbols/BlockSymbols.h>
#include <slang/ast/symbols/CompilationUnitSymbols.h>
#include <slang/ast/symbols/InstanceSymbols.h>
#include <slang/ast/symbols/MemberSymbols.h>
#include <slang/ast/symbols/SubroutineSymbols.h>
#include <slang/ast/types/AllTypes.h>

#include "slangd/utils/conversion.hpp"

namespace slangd::semantic {

auto ComputeLspRange(
    const slang::ast::Symbol& symbol,
    const slang::SourceManager& source_manager) -> lsp::Range {
  if (symbol.location) {
    return ToLspRange(symbol.location, source_manager);
  }
  // Return zero range for symbols without location
  return lsp::Range{
      .start = {.line = 0, .character = 0}, .end = {.line = 0, .character = 0}};
}

auto ShouldIndexForSemanticIndex(const slang::ast::Symbol& symbol) -> bool {
  using SK = slang::ast::SymbolKind;

  // Always index packages - they're important for go-to-definition
  if (symbol.kind == SK::Package) {
    return symbol.location.valid();
  }

  // Skip symbols without names (except some special cases)
  if (symbol.name.empty()) {
    // Allow some unnamed symbols that are still useful
    switch (symbol.kind) {
      case SK::CompilationUnit:
      case SK::InstanceBody:
      case SK::Instance:
      case SK::GenerateBlock:       // Allow unnamed generate blocks
      case SK::GenerateBlockArray:  // Allow unnamed generate block arrays
        break;                      // Allow these unnamed symbols
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

auto ShouldIndexForDocumentSymbols(const slang::ast::Symbol& symbol) -> bool {
  using SK = slang::ast::SymbolKind;

  // Filter out genvar loop variables - they're just counters, not meaningful
  // symbols
  if (symbol.kind == SK::Genvar) {
    return false;
  }

  // Index most symbol types for document symbols
  switch (symbol.kind) {
    case SK::Package:
    case SK::Definition:    // Modules, interfaces, programs
    case SK::InstanceBody:  // Module/interface instance bodies
    case SK::Variable:
    case SK::Parameter:
    case SK::Port:
    case SK::TypeAlias:
    case SK::StatementBlock:
    case SK::ProceduralBlock:
    case SK::GenerateBlock:
    case SK::GenerateBlockArray:
    case SK::Subroutine:       // Functions and tasks
    case SK::MethodPrototype:  // Pure virtual functions
    case SK::EnumValue:
    case SK::Field:  // Struct/union fields
      return true;
    default:
      return false;
  }
}

auto ConvertToLspKind(const slang::ast::Symbol& symbol) -> lsp::SymbolKind {
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
    case SK::GenericClassDef:
      return LK::kClass;

    // Interface-related
    case SK::Modport:
      return LK::kInterface;

    // Function-related (both functions and tasks)
    case SK::Subroutine:
    case SK::MethodPrototype:  // Pure virtual functions
      return LK::kFunction;

    // Generate blocks are containers/namespaces
    case SK::GenerateBlock:
    case SK::GenerateBlockArray:
      return LK::kNamespace;

    // Statement blocks - runtime controllable constructs vs scope containers
    case SK::StatementBlock:
      if (!symbol.name.empty()) {
        // Named statement blocks are typically runtime controllable constructs
        // (assertions, fork/join, etc.) that can be enabled/disabled by name
        // Map to Variable since they represent runtime behavior
        return LK::kVariable;
      }
      // Unnamed statement blocks are always namespaces/containers
      return LK::kNamespace;

    // Default for other symbol kinds
    default:
      return LK::kObject;
  }
}

auto ConvertToLspKindForDocuments(const slang::ast::Symbol& symbol)
    -> lsp::SymbolKind {
  using SK = slang::ast::SymbolKind;

  switch (symbol.kind) {
    case SK::Package:
      return lsp::SymbolKind::kPackage;
    case SK::Definition:
      return lsp::SymbolKind::kModule;
    case SK::InstanceBody:
      return lsp::SymbolKind::kClass;
    case SK::Variable:
      return lsp::SymbolKind::kVariable;
    case SK::Parameter:
      return lsp::SymbolKind::kConstant;
    case SK::Port:
      return lsp::SymbolKind::kInterface;
    case SK::TypeAlias:
      return lsp::SymbolKind::kStruct;
    case SK::StatementBlock:
      // Named statement blocks are typically runtime controllable constructs
      if (!symbol.name.empty()) {
        return lsp::SymbolKind::kVariable;
      }
      return lsp::SymbolKind::kNamespace;
    case SK::ProceduralBlock:
      return lsp::SymbolKind::kNamespace;
    case SK::GenerateBlock:
    case SK::GenerateBlockArray:
      return lsp::SymbolKind::kNamespace;
    case SK::Subroutine:
    case SK::MethodPrototype:  // Pure virtual functions
      return lsp::SymbolKind::kFunction;
    case SK::EnumValue:
      return lsp::SymbolKind::kEnumMember;
    case SK::Field:
      return lsp::SymbolKind::kField;
    default:
      return lsp::SymbolKind::kVariable;
  }
}

auto UnwrapSymbol(const slang::ast::Symbol& symbol)
    -> const slang::ast::Symbol& {
  using SK = slang::ast::SymbolKind;

  // If not a TransparentMember, return directly
  if (symbol.kind != SK::TransparentMember) {
    return symbol;
  }

  // Recursively unwrap
  return UnwrapSymbol(symbol.as<slang::ast::TransparentMemberSymbol>().wrapped);
}

}  // namespace slangd::semantic

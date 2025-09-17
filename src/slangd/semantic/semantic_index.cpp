#include "slangd/semantic/semantic_index.hpp"

#include <slang/ast/Compilation.h>
#include <slang/ast/Symbol.h>
#include <slang/ast/symbols/CompilationUnitSymbols.h>
#include <slang/ast/symbols/InstanceSymbols.h>
#include <slang/ast/symbols/MemberSymbols.h>
#include <slang/ast/symbols/ParameterSymbols.h>
#include <slang/ast/symbols/VariableSymbols.h>
#include <slang/ast/types/AllTypes.h>

#include "slangd/utils/conversion.hpp"

namespace slangd::semantic {

auto SemanticIndex::FromCompilation(
    slang::ast::Compilation& compilation,
    const slang::SourceManager& source_manager)
    -> std::unique_ptr<SemanticIndex> {
  auto index = std::unique_ptr<SemanticIndex>(new SemanticIndex());

  // Create visitor for comprehensive symbol collection
  auto visitor = IndexVisitor([&](const slang::ast::Symbol& symbol) {
    // Only index symbols that meet basic criteria
    if (!ShouldIndex(symbol)) {
      return;
    }

    // Unwrap symbol to handle TransparentMember recursion
    const auto& unwrapped = UnwrapSymbol(symbol);

    // Create SymbolInfo with cached LSP data
    SymbolInfo info{
        .symbol = &unwrapped,
        .location = unwrapped.location,
        .lsp_kind = ConvertToLspKind(unwrapped),
        .range = ComputeLspRange(unwrapped, source_manager),
        .parent = unwrapped.getParentScope()};

    // Store in flat map for O(1) lookup
    index->symbols_[unwrapped.location] = info;
  });

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

  // EXACT COPY from legacy symbol_index.cpp
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

}  // namespace slangd::semantic

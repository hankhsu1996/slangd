#include "slangd/semantic/semantic_index.hpp"

#include <filesystem>

#include <slang/ast/Compilation.h>
#include <slang/ast/Symbol.h>
#include <slang/ast/expressions/CallExpression.h>
#include <slang/ast/expressions/ConversionExpression.h>
#include <slang/ast/expressions/MiscExpressions.h>
#include <slang/ast/symbols/BlockSymbols.h>
#include <slang/ast/symbols/CompilationUnitSymbols.h>
#include <slang/ast/symbols/InstanceSymbols.h>
#include <slang/ast/symbols/MemberSymbols.h>
#include <slang/ast/symbols/ParameterSymbols.h>
#include <slang/ast/symbols/PortSymbols.h>
#include <slang/ast/symbols/VariableSymbols.h>
#include <slang/ast/types/AllTypes.h>
#include <slang/syntax/AllSyntax.h>
#include <slang/util/Enum.h>
#include <spdlog/spdlog.h>

#include "slangd/semantic/definition_extractor.hpp"
#include "slangd/semantic/document_symbol_builder.hpp"
#include "slangd/semantic/symbol_utils.hpp"
#include "slangd/utils/conversion.hpp"

namespace slangd::semantic {

auto SemanticIndex::FromCompilation(
    slang::ast::Compilation& compilation,
    const slang::SourceManager& source_manager,
    const std::string& current_file_uri) -> std::unique_ptr<SemanticIndex> {
  auto index = std::unique_ptr<SemanticIndex>(new SemanticIndex());

  // Store source manager reference for document symbol processing
  index->source_manager_ = &source_manager;

  // Create visitor for comprehensive symbol collection and reference tracking
  auto visitor = IndexVisitor(index.get(), &source_manager, current_file_uri);

  const auto& root = compilation.getRoot();

  // OPTIMIZATION: Find and visit ALL compilation units from current file
  std::vector<const slang::ast::Symbol*> target_units;

  // Normalize URIs for comparison - handle relative paths from Slang
  auto normalize_uri = [](const std::string& uri) -> std::string {
    if (uri.starts_with("file://")) {
      try {
        auto path = std::filesystem::path(uri.substr(7));  // Remove "file://"
        auto canonical = std::filesystem::weakly_canonical(path);
        return "file://" + canonical.string();
      } catch (...) {
        return uri;  // Return original if normalization fails
      }
    }
    return uri;
  };

  auto normalized_target = normalize_uri(current_file_uri);

  for (const auto& member : root.members()) {
    bool should_visit = false;

    if (member.location.valid()) {
      auto member_uri = std::string(
          ConvertSlangLocationToLspLocation(member.location, source_manager)
              .uri);

      auto normalized_member = normalize_uri(member_uri);

      if (normalized_member == normalized_target) {
        should_visit = true;
      }
    } else if (member.kind == slang::ast::SymbolKind::CompilationUnit) {
      // Visit CompilationUnits that might contain symbols from current file
      // Check if any of the CompilationUnit's members are from the target file
      bool contains_target_symbols = false;

      if (member.isScope()) {
        const auto& scope = member.as<slang::ast::Scope>();
        for (const auto& child : scope.members()) {
          if (child.location.valid()) {
            auto child_uri = std::string(ConvertSlangLocationToLspLocation(
                                             child.location, source_manager)
                                             .uri);
            if (normalize_uri(child_uri) == normalized_target) {
              contains_target_symbols = true;
              break;
            }
          }
        }
      }

      if (contains_target_symbols) {
        should_visit = true;
      }
    }

    if (should_visit) {
      target_units.push_back(&member);
    }
  }

  // Also look for Definition symbols (interfaces, modules) using
  // compilation.getDefinitions()
  std::vector<const slang::ast::Symbol*> target_definitions;
  auto definitions = compilation.getDefinitions();
  for (const auto* def : definitions) {
    if (def->location.valid()) {
      auto def_uri = std::string(
          ConvertSlangLocationToLspLocation(def->location, source_manager).uri);
      auto normalized_def = normalize_uri(def_uri);

      if (normalized_def == normalized_target) {
        target_definitions.push_back(def);
      }
    }
  }

  if (!target_units.empty()) {
    for (const auto* unit : target_units) {
      unit->visit(visitor);
    }
  }

  if (!target_definitions.empty()) {
    for (const auto* def : target_definitions) {
      def->visit(visitor);
    }
  }

  if (target_units.empty() && target_definitions.empty()) {
    // Don't visit anything - return empty index for performance
    // This is expected behavior when no matching symbols are found
  }

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
  return DocumentSymbolBuilder::BuildDocumentSymbolTree(uri, *this);
}

// IndexVisitor implementation
void SemanticIndex::IndexVisitor::ProcessSymbol(
    const slang::ast::Symbol& symbol) {
  // Skip individual GenerateBlockSymbol entries when they're part of a
  // GenerateBlockArraySymbol We'll handle these through the special template
  // extraction logic instead
  if (symbol.kind == slang::ast::SymbolKind::GenerateBlock) {
    const auto* parent_scope = symbol.getParentScope();
    if ((parent_scope != nullptr) &&
        parent_scope->asSymbol().kind ==
            slang::ast::SymbolKind::GenerateBlockArray) {
      return;
    }
  }

  // Only index symbols that meet basic criteria
  if (!ShouldIndexForSemanticIndex(symbol)) {
    return;
  }

  // Unwrap symbol to handle TransparentMember recursion
  const auto& unwrapped = UnwrapSymbol(symbol);

  // Extract precise definition range from syntax node
  slang::SourceRange definition_range;
  bool is_definition = false;
  if (const auto* syntax = unwrapped.getSyntax()) {
    definition_range =
        DefinitionExtractor::ExtractDefinitionRange(unwrapped, *syntax);
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
      .lsp_kind = ConvertToLspKind(unwrapped),
      .range = ComputeLspRange(unwrapped, *source_manager_),
      .parent = unwrapped.getParentScope(),
      .is_definition = is_definition,
      .definition_range = definition_range,
      .buffer_id = unwrapped.location.buffer().getId()};

  // Store in flat map for O(1) lookup
  // Priority handling: prefer named symbols and GenerateBlockArray over unnamed
  // GenerateBlocks
  auto existing_it = index_->symbols_.find(unwrapped.location);
  bool should_store = true;
  if (existing_it != index_->symbols_.end()) {
    const auto& existing_info = existing_it->second;

    // Prefer named symbols over unnamed ones
    if (!unwrapped.name.empty() && existing_info.symbol->name.empty()) {
      // Named symbol takes precedence over unnamed symbol
      should_store = true;
    } else if (unwrapped.name.empty() && !existing_info.symbol->name.empty()) {
      // Keep named symbol over unnamed symbol
      should_store = false;
    } else if (
        unwrapped.kind == slang::ast::SymbolKind::Subroutine &&
        existing_info.symbol->kind == slang::ast::SymbolKind::Variable) {
      // Prefer Subroutine over Variable for functions (both have same
      // name/location)
      should_store = true;
    } else if (
        unwrapped.kind == slang::ast::SymbolKind::Variable &&
        existing_info.symbol->kind == slang::ast::SymbolKind::Subroutine) {
      // Subroutine takes precedence over Variable
      should_store = false;
    } else if (
        unwrapped.kind == slang::ast::SymbolKind::GenerateBlockArray &&
        existing_info.symbol->kind == slang::ast::SymbolKind::GenerateBlock) {
      // Prefer GenerateBlockArray over GenerateBlock
      should_store = true;
    } else if (
        unwrapped.kind == slang::ast::SymbolKind::GenerateBlock &&
        existing_info.symbol->kind ==
            slang::ast::SymbolKind::GenerateBlockArray) {
      // GenerateBlockArray takes precedence over GenerateBlock
      should_store = false;
    }
  }

  if (should_store) {
    index_->symbols_[unwrapped.location] = info;
  }
}

void SemanticIndex::IndexVisitor::TraverseType(const slang::ast::Type& type) {
  switch (type.kind) {
    case slang::ast::SymbolKind::PackedArrayType: {
      const auto& packed_array = type.as<slang::ast::PackedArrayType>();
      packed_array.evalDim.visitExpressions(
          [this](const slang::ast::Expression& expr) -> void {
            expr.visit(*this);
          });
      TraverseType(packed_array.elementType);
      break;
    }
    case slang::ast::SymbolKind::FixedSizeUnpackedArrayType: {
      const auto& unpacked_array =
          type.as<slang::ast::FixedSizeUnpackedArrayType>();
      unpacked_array.evalDim.visitExpressions(
          [this](const slang::ast::Expression& expr) -> void {
            expr.visit(*this);
          });
      TraverseType(unpacked_array.elementType);
      break;
    }
    case slang::ast::SymbolKind::DynamicArrayType: {
      const auto& dynamic_array = type.as<slang::ast::DynamicArrayType>();
      TraverseType(dynamic_array.elementType);
      break;
    }
    case slang::ast::SymbolKind::QueueType: {
      const auto& queue_type = type.as<slang::ast::QueueType>();
      queue_type.evalDim.visitExpressions(
          [this](const slang::ast::Expression& expr) -> void {
            expr.visit(*this);
          });
      TraverseType(queue_type.elementType);
      break;
    }
    case slang::ast::SymbolKind::AssociativeArrayType: {
      const auto& assoc_array = type.as<slang::ast::AssociativeArrayType>();
      TraverseType(assoc_array.elementType);
      break;
    }
    case slang::ast::SymbolKind::TypeAlias: {
      const auto& type_alias = type.as<slang::ast::TypeAliasType>();
      TraverseType(type_alias.targetType.getType());
      break;
    }
    case slang::ast::SymbolKind::TypeReference: {
      const auto& type_ref = type.as<slang::ast::TypeReferenceSymbol>();
      if (const auto* typedef_target =
              type_ref.getResolvedType().as_if<slang::ast::TypeAliasType>()) {
        if (typedef_target->location.valid()) {
          if (const auto* syntax = typedef_target->getSyntax()) {
            auto definition_range = DefinitionExtractor::ExtractDefinitionRange(
                *typedef_target, *syntax);
            CreateReference(
                type_ref.getUsageLocation(), definition_range, *typedef_target);
          }
        }
      }
      break;
    }
    case slang::ast::SymbolKind::EnumType: {
      const auto& enum_type = type.as<slang::ast::EnumType>();
      for (const auto& enum_value : enum_type.values()) {
        this->visit(enum_value);
      }
      break;
    }
    case slang::ast::SymbolKind::PackedStructType: {
      const auto& struct_type = type.as<slang::ast::PackedStructType>();
      for (const auto& field :
           struct_type.membersOfType<slang::ast::FieldSymbol>()) {
        this->visit(field);
      }
      break;
    }
    case slang::ast::SymbolKind::UnpackedStructType: {
      const auto& struct_type = type.as<slang::ast::UnpackedStructType>();
      for (const auto& field : struct_type.fields) {
        this->visit(*field);
      }
      break;
    }
    case slang::ast::SymbolKind::PackedUnionType: {
      const auto& union_type = type.as<slang::ast::PackedUnionType>();
      for (const auto& field :
           union_type.membersOfType<slang::ast::FieldSymbol>()) {
        this->visit(field);
      }
      break;
    }
    case slang::ast::SymbolKind::UnpackedUnionType: {
      const auto& union_type = type.as<slang::ast::UnpackedUnionType>();
      for (const auto& field : union_type.fields) {
        this->visit(*field);
      }
      break;
    }
    default:
      break;
  }
}

void SemanticIndex::IndexVisitor::CreateReference(
    slang::SourceRange source_range, slang::SourceRange definition_range,
    const slang::ast::Symbol& target_symbol) {
  ReferenceEntry ref{
      .source_range = source_range,
      .target_loc = target_symbol.location,
      .target_range = definition_range,
      .symbol_kind = ConvertToLspKind(target_symbol),
      .symbol_name = std::string(target_symbol.name)};
  index_->references_.push_back(ref);
}

void SemanticIndex::IndexVisitor::handle(
    const slang::ast::NamedValueExpression& expr) {
  const slang::ast::Symbol* target_symbol = &expr.symbol;

  if (expr.symbol.kind == slang::ast::SymbolKind::ExplicitImport) {
    const auto& import_symbol =
        expr.symbol.as<slang::ast::ExplicitImportSymbol>();
    const auto* imported_symbol = import_symbol.importedSymbol();
    if (imported_symbol != nullptr) {
      target_symbol = imported_symbol;
    }
  }

  // Handle compiler-generated function return variables
  // When referencing the implicit return variable (e.g., my_func = value),
  // redirect to the parent subroutine for better UX
  if (expr.symbol.kind == slang::ast::SymbolKind::Variable) {
    const auto& variable = expr.symbol.as<slang::ast::VariableSymbol>();
    if (variable.flags.has(slang::ast::VariableFlags::CompilerGenerated)) {
      const auto* parent_scope = variable.getParentScope();
      if (parent_scope != nullptr) {
        const auto& parent_symbol = parent_scope->asSymbol();
        if (parent_symbol.kind == slang::ast::SymbolKind::Subroutine) {
          target_symbol = &parent_symbol;
        }
      }
    }
  }

  if (target_symbol->location.valid()) {
    if (const auto* syntax = target_symbol->getSyntax()) {
      auto definition_range =
          DefinitionExtractor::ExtractDefinitionRange(*target_symbol, *syntax);
      CreateReference(expr.sourceRange, definition_range, *target_symbol);
    }
  }
  this->visitDefault(expr);
}

void SemanticIndex::IndexVisitor::handle(
    const slang::ast::CallExpression& expr) {
  // Handle references to subroutines (functions/tasks) in calls
  if (!expr.isSystemCall()) {
    if (const auto* subroutine_symbol = std::get_if<0>(&expr.subroutine)) {
      if ((*subroutine_symbol)->location.valid()) {
        if (const auto* syntax = (*subroutine_symbol)->getSyntax()) {
          auto definition_range = DefinitionExtractor::ExtractDefinitionRange(
              **subroutine_symbol, *syntax);
          CreateReference(
              expr.sourceRange, definition_range, **subroutine_symbol);
        }
      }
    }
  }
  this->visitDefault(expr);
}

void SemanticIndex::IndexVisitor::handle(
    const slang::ast::ConversionExpression& expr) {
  TraverseType(*expr.type);
  this->visitDefault(expr);
}

void SemanticIndex::IndexVisitor::handle(
    const slang::ast::MemberAccessExpression& expr) {
  if (expr.member.location.valid()) {
    if (const auto* syntax = expr.member.getSyntax()) {
      auto definition_range =
          DefinitionExtractor::ExtractDefinitionRange(expr.member, *syntax);
      CreateReference(expr.memberNameRange(), definition_range, expr.member);
    }
  }
  this->visitDefault(expr);
}

void SemanticIndex::IndexVisitor::handle(
    const slang::ast::VariableSymbol& symbol) {
  TraverseType(symbol.getType());
  this->visitDefault(symbol);
}

void SemanticIndex::IndexVisitor::handle(
    const slang::ast::WildcardImportSymbol& import_symbol) {
  const auto* package = import_symbol.getPackage();
  if (package == nullptr || !package->location.valid()) {
    this->visitDefault(import_symbol);
    return;
  }

  const auto* import_syntax = import_symbol.getSyntax();
  if (import_syntax == nullptr ||
      import_syntax->kind != slang::syntax::SyntaxKind::PackageImportItem) {
    this->visitDefault(import_symbol);
    return;
  }

  const auto& import_item =
      import_syntax->as<slang::syntax::PackageImportItemSyntax>();
  const auto* pkg_syntax = package->getSyntax();
  if (pkg_syntax == nullptr) {
    this->visitDefault(import_symbol);
    return;
  }

  auto definition_range =
      DefinitionExtractor::ExtractDefinitionRange(*package, *pkg_syntax);
  CreateReference(import_item.package.range(), definition_range, *package);
  this->visitDefault(import_symbol);
}

void SemanticIndex::IndexVisitor::handle(
    const slang::ast::ExplicitImportSymbol& import_symbol) {
  const auto* package = import_symbol.package();
  if (package == nullptr || !package->location.valid()) {
    this->visitDefault(import_symbol);
    return;
  }

  const auto* import_syntax = import_symbol.getSyntax();
  if (import_syntax == nullptr ||
      import_syntax->kind != slang::syntax::SyntaxKind::PackageImportItem) {
    this->visitDefault(import_symbol);
    return;
  }

  const auto& import_item =
      import_syntax->as<slang::syntax::PackageImportItemSyntax>();
  const auto* pkg_syntax = package->getSyntax();
  if (pkg_syntax == nullptr) {
    this->visitDefault(import_symbol);
    return;
  }

  auto definition_range =
      DefinitionExtractor::ExtractDefinitionRange(*package, *pkg_syntax);
  CreateReference(import_item.package.range(), definition_range, *package);

  // Create reference for the imported symbol name
  const auto* imported_symbol = import_symbol.importedSymbol();
  if (imported_symbol != nullptr) {
    if (imported_symbol->location.valid()) {
      if (const auto* imported_syntax = imported_symbol->getSyntax()) {
        auto imported_definition_range =
            DefinitionExtractor::ExtractDefinitionRange(
                *imported_symbol, *imported_syntax);
        CreateReference(
            import_item.item.range(), imported_definition_range,
            *imported_symbol);
      }
    }
  }

  this->visitDefault(import_symbol);
}

void SemanticIndex::IndexVisitor::handle(
    const slang::ast::ParameterSymbol& param) {
  if (param.location.valid()) {
    if (const auto* syntax = param.getSyntax()) {
      auto definition_range =
          DefinitionExtractor::ExtractDefinitionRange(param, *syntax);
      CreateReference(definition_range, definition_range, param);
    }
  }

  TraverseType(param.getType());
  this->visitDefault(param);
}

void SemanticIndex::IndexVisitor::handle(
    const slang::ast::SubroutineSymbol& subroutine) {
  if (subroutine.location.valid()) {
    if (const auto* syntax = subroutine.getSyntax()) {
      auto definition_range =
          DefinitionExtractor::ExtractDefinitionRange(subroutine, *syntax);
      CreateReference(definition_range, definition_range, subroutine);
    }
  }
  this->visitDefault(subroutine);
}

void SemanticIndex::IndexVisitor::handle(
    const slang::ast::DefinitionSymbol& definition) {
  if (definition.location.valid()) {
    if (const auto* syntax = definition.getSyntax()) {
      auto definition_range =
          DefinitionExtractor::ExtractDefinitionRange(definition, *syntax);
      CreateReference(definition_range, definition_range, definition);
    }
  }
  this->visitDefault(definition);
}

void SemanticIndex::IndexVisitor::handle(
    const slang::ast::TypeAliasType& type_alias) {
  if (type_alias.location.valid()) {
    if (const auto* syntax = type_alias.getSyntax()) {
      if (syntax->kind == slang::syntax::SyntaxKind::TypedefDeclaration) {
        auto definition_range =
            syntax->as<slang::syntax::TypedefDeclarationSyntax>().name.range();
        CreateReference(definition_range, definition_range, type_alias);
      }
    }
  }

  TraverseType(type_alias.targetType.getType());
  this->visitDefault(type_alias);
}

void SemanticIndex::IndexVisitor::handle(
    const slang::ast::EnumValueSymbol& enum_value) {
  if (enum_value.location.valid()) {
    if (const auto* syntax = enum_value.getSyntax()) {
      auto definition_range =
          DefinitionExtractor::ExtractDefinitionRange(enum_value, *syntax);
      CreateReference(definition_range, definition_range, enum_value);
    }
  }
  this->visitDefault(enum_value);
}

void SemanticIndex::IndexVisitor::handle(const slang::ast::FieldSymbol& field) {
  if (field.location.valid()) {
    if (const auto* syntax = field.getSyntax()) {
      auto definition_range =
          DefinitionExtractor::ExtractDefinitionRange(field, *syntax);
      CreateReference(definition_range, definition_range, field);
    }
  }

  TraverseType(field.getType());
  this->visitDefault(field);
}

void SemanticIndex::IndexVisitor::handle(const slang::ast::NetSymbol& net) {
  if (net.location.valid()) {
    if (const auto* syntax = net.getSyntax()) {
      auto definition_range =
          DefinitionExtractor::ExtractDefinitionRange(net, *syntax);
      CreateReference(definition_range, definition_range, net);
    }
  }

  TraverseType(net.getType());
  this->visitDefault(net);
}

void SemanticIndex::IndexVisitor::handle(const slang::ast::PortSymbol& port) {
  if (port.location.valid()) {
    if (const auto* syntax = port.getSyntax()) {
      auto definition_range =
          DefinitionExtractor::ExtractDefinitionRange(port, *syntax);
      CreateReference(definition_range, definition_range, port);
    }
  }

  TraverseType(port.getType());
  this->visitDefault(port);
}

void SemanticIndex::IndexVisitor::handle(
    const slang::ast::InterfacePortSymbol& interface_port) {
  if (interface_port.location.valid()) {
    if (const auto* syntax = interface_port.getSyntax()) {
      auto definition_range =
          DefinitionExtractor::ExtractDefinitionRange(interface_port, *syntax);
      CreateReference(definition_range, definition_range, interface_port);

      // Create cross-references from interface port syntax to target symbols
      // Interface name cross-reference (MemBus -> interface definition)
      if (interface_port.interfaceDef != nullptr) {
        slang::SourceRange interface_name_range =
            interface_port.interfaceNameRange();
        if (interface_name_range.start().valid()) {
          if (interface_port.interfaceDef->location.valid()) {
            if (const auto* interface_syntax =
                    interface_port.interfaceDef->getSyntax()) {
              auto interface_definition_range =
                  DefinitionExtractor::ExtractDefinitionRange(
                      *interface_port.interfaceDef, *interface_syntax);
              CreateReference(
                  interface_name_range, interface_definition_range,
                  *interface_port.interfaceDef);
            }
          }
        }
      }

      // Modport name cross-reference (cpu -> modport definition)
      if (!interface_port.modport.empty()) {
        auto modport_range = interface_port.modportNameRange();
        if (modport_range.start().valid()) {
          // Use existing Slang resolution logic to get ModportSymbol
          auto connection = interface_port.getConnection();
          if (connection.second != nullptr) {
            if (connection.second->location.valid()) {
              if (const auto* modport_syntax = connection.second->getSyntax()) {
                auto modport_definition_range =
                    DefinitionExtractor::ExtractDefinitionRange(
                        *connection.second, *modport_syntax);
                CreateReference(
                    modport_range, modport_definition_range,
                    *connection.second);
              }
            }
          }
        }
      }
    }
  }
  this->visitDefault(interface_port);
}

void SemanticIndex::IndexVisitor::handle(
    const slang::ast::ModportSymbol& modport) {
  if (modport.location.valid()) {
    if (const auto* syntax = modport.getSyntax()) {
      auto definition_range =
          DefinitionExtractor::ExtractDefinitionRange(modport, *syntax);
      CreateReference(definition_range, definition_range, modport);
    }
  }
  this->visitDefault(modport);
}

void SemanticIndex::IndexVisitor::handle(
    const slang::ast::ModportPortSymbol& modport_port) {
  if (modport_port.location.valid()) {
    if (const auto* syntax = modport_port.getSyntax()) {
      auto definition_range =
          DefinitionExtractor::ExtractDefinitionRange(modport_port, *syntax);
      CreateReference(definition_range, definition_range, modport_port);
    }
  }
  this->visitDefault(modport_port);
}

void SemanticIndex::IndexVisitor::handle(
    const slang::ast::GenerateBlockSymbol& generate_block) {
  if (generate_block.location.valid()) {
    if (const auto* syntax = generate_block.getSyntax()) {
      auto definition_range =
          DefinitionExtractor::ExtractDefinitionRange(generate_block, *syntax);
      CreateReference(definition_range, definition_range, generate_block);
    }
  }
  this->visitDefault(generate_block);
}

void SemanticIndex::IndexVisitor::handle(
    const slang::ast::GenvarSymbol& genvar) {
  if (genvar.location.valid()) {
    if (const auto* syntax = genvar.getSyntax()) {
      auto definition_range =
          DefinitionExtractor::ExtractDefinitionRange(genvar, *syntax);
      CreateReference(definition_range, definition_range, genvar);
    }
  }
  this->visitDefault(genvar);
}

// Go-to-definition implementation
auto SemanticIndex::LookupDefinitionAt(slang::SourceLocation loc) const
    -> std::optional<slang::SourceRange> {
  // Direct lookup using unified reference storage
  // NOTE: Ranges should NOT overlap - if they do, fix the root cause in
  // reference creation
  for (const auto& ref_entry : references_) {
    if (ref_entry.source_range.contains(loc)) {
      return ref_entry.target_range;
    }
  }
  return std::nullopt;
}

}  // namespace slangd::semantic

#include "slangd/semantic/semantic_index.hpp"

#include <filesystem>

#include <slang/ast/Compilation.h>
#include <slang/ast/HierarchicalReference.h>
#include <slang/ast/Symbol.h>
#include <slang/ast/expressions/CallExpression.h>
#include <slang/ast/expressions/ConversionExpression.h>
#include <slang/ast/expressions/MiscExpressions.h>
#include <slang/ast/expressions/SelectExpressions.h>
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

  // Also visit any instances in the compilation root that might have been
  // auto-generated This is specifically needed for LSP mode where interface
  // instances are auto-created
  for (const auto& member : root.members()) {
    if (member.kind == slang::ast::SymbolKind::Instance) {
      const auto& instance = member.as<slang::ast::InstanceSymbol>();

      // Visit interface instances regardless of source location to capture
      // auto-generated ones
      if (instance.isInterface()) {
        instance.visit(visitor);
      }
    }
  }

  if (target_units.empty() && target_definitions.empty()) {
    // Don't visit anything - return empty index for performance
    // This is expected behavior when no matching symbols are found
  }

  index->ValidateNoRangeOverlaps();

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

      // Slang ARCHITECTURAL LIMITATION WORKAROUND:
      // For expressions like `data[i]`, Slang creates NamedValueExpression with
      // the entire expression range (data[i]) instead of just the symbol range
      // (data).
      //
      // Root cause: Even though we have the symbol and can access its syntax,
      // Slang provides no way to break down composite expressions into
      // components. The ElementSelectExpression syntax gives us the full range,
      // not the name part.
      //
      // Solution: Always trim to symbol name length for universal, predictable
      // behavior. Traced to: slang/source/parsing/Parser_expressions.cpp
      // parsePostfixExpression()

      // Universal path: always use symbol name length for precise reference
      // ranges
      auto reference_range = slang::SourceRange(
          expr.sourceRange.start(),
          expr.sourceRange.start() +
              static_cast<uint32_t>(target_symbol->name.length()));

      CreateReference(reference_range, definition_range, *target_symbol);
    }
  }
  this->visitDefault(expr);
}

void SemanticIndex::IndexVisitor::handle(
    const slang::ast::CallExpression& expr) {
  // Only handle user-defined subroutine calls, not system calls
  if (expr.isSystemCall()) {
    this->visitDefault(expr);
    return;
  }

  const auto* subroutine_symbol = std::get_if<0>(&expr.subroutine);
  if (subroutine_symbol == nullptr || !(*subroutine_symbol)->location.valid()) {
    this->visitDefault(expr);
    return;
  }

  // Modern approach: use std::optional and lambdas for clean range extraction
  auto extract_definition_range = [&]() -> std::optional<slang::SourceRange> {
    const auto* syntax = (*subroutine_symbol)->getSyntax();
    if (syntax == nullptr) {
      return std::nullopt;
    }

    if (syntax->kind == slang::syntax::SyntaxKind::TaskDeclaration ||
        syntax->kind == slang::syntax::SyntaxKind::FunctionDeclaration) {
      const auto& func_syntax =
          syntax->as<slang::syntax::FunctionDeclarationSyntax>();
      if (func_syntax.prototype != nullptr &&
          func_syntax.prototype->name != nullptr) {
        return func_syntax.prototype->name->sourceRange();
      }
    }
    return std::nullopt;
  };

  auto extract_call_range = [&]() -> std::optional<slang::SourceRange> {
    if (expr.syntax == nullptr) {
      return std::nullopt;
    }

    if (expr.syntax->kind == slang::syntax::SyntaxKind::InvocationExpression) {
      const auto& invocation =
          expr.syntax->as<slang::syntax::InvocationExpressionSyntax>();
      return invocation.left->sourceRange();
    }

    if (expr.syntax->kind ==
        slang::syntax::SyntaxKind::ArrayOrRandomizeMethodExpression) {
      const auto& method =
          expr.syntax
              ->as<slang::syntax::ArrayOrRandomizeMethodExpressionSyntax>();
      if (method.method != nullptr) {
        return method.method->sourceRange();
      }
    }

    return std::nullopt;
  };

  auto definition_range = extract_definition_range();
  auto call_range = extract_call_range();

  if (!definition_range || !call_range) {
    this->visitDefault(expr);
    return;
  }

  CreateReference(*call_range, *definition_range, **subroutine_symbol);
  this->visitDefault(expr);
}

void SemanticIndex::IndexVisitor::handle(
    const slang::ast::ConversionExpression& expr) {
  // Only process explicit user-written type casts (e.g., type_name'(value))
  // Skip implicit compiler-generated conversions to avoid duplicates
  if (!expr.isImplicit()) {
    TraverseType(*expr.type);
  }
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
    const slang::ast::HierarchicalValueExpression& expr) {
  const slang::ast::Symbol* target_symbol = &expr.symbol;

  // If this is a ModportPortSymbol, trace to the underlying variable
  if (expr.symbol.kind == slang::ast::SymbolKind::ModportPort) {
    const auto& modport_port = expr.symbol.as<slang::ast::ModportPortSymbol>();
    if (modport_port.internalSymbol != nullptr) {
      target_symbol = modport_port.internalSymbol;
    }
  }

  if (target_symbol->location.valid()) {
    if (const auto* syntax = target_symbol->getSyntax()) {
      auto definition_range =
          DefinitionExtractor::ExtractDefinitionRange(*target_symbol, *syntax);

      // Use precise symbol name range, similar to NamedValueExpression approach
      auto reference_range = slang::SourceRange(
          expr.sourceRange.start(),
          expr.sourceRange.start() +
              static_cast<uint32_t>(target_symbol->name.length()));

      CreateReference(reference_range, definition_range, *target_symbol);
    }
  }
  this->visitDefault(expr);
}

void SemanticIndex::IndexVisitor::handle(
    const slang::ast::VariableSymbol& symbol) {
  if (!symbol.location.valid()) {
    TraverseType(symbol.getType());
    this->visitDefault(symbol);
    return;
  }

  const auto* syntax = symbol.getSyntax();
  if (syntax == nullptr) {
    TraverseType(symbol.getType());
    this->visitDefault(symbol);
    return;
  }

  // Handle different variable declaration patterns
  switch (syntax->kind) {
    case slang::syntax::SyntaxKind::DataDeclaration: {
      // Find specific declarator to avoid type reference overlaps
      const auto& data_decl =
          syntax->as<slang::syntax::DataDeclarationSyntax>();
      for (const auto& declarator : data_decl.declarators) {
        if (declarator != nullptr &&
            declarator->name.valueText() == symbol.name) {
          auto definition_range = declarator->name.range();
          CreateReference(definition_range, definition_range, symbol);
          break;
        }
      }
      break;
    }
    case slang::syntax::SyntaxKind::ForVariableDeclaration:
    case slang::syntax::SyntaxKind::CheckerDataDeclaration: {
      auto definition_range = syntax->sourceRange();
      CreateReference(definition_range, definition_range, symbol);
      break;
    }
    case slang::syntax::SyntaxKind::Declarator: {
      const auto& decl_syntax = syntax->as<slang::syntax::DeclaratorSyntax>();
      auto definition_range = decl_syntax.name.range();
      CreateReference(definition_range, definition_range, symbol);
      break;
    }
    default:
      // Unknown syntax kind - skip reference creation
      break;
  }

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
      if (syntax->kind == slang::syntax::SyntaxKind::Declarator) {
        auto definition_range =
            syntax->as<slang::syntax::DeclaratorSyntax>().name.range();
        CreateReference(definition_range, definition_range, param);
      }
    }
  }

  TraverseType(param.getType());
  this->visitDefault(param);
}

void SemanticIndex::IndexVisitor::handle(
    const slang::ast::SubroutineSymbol& subroutine) {
  if (subroutine.location.valid()) {
    if (const auto* syntax = subroutine.getSyntax()) {
      if (syntax->kind == slang::syntax::SyntaxKind::TaskDeclaration ||
          syntax->kind == slang::syntax::SyntaxKind::FunctionDeclaration) {
        const auto& func_syntax =
            syntax->as<slang::syntax::FunctionDeclarationSyntax>();
        if ((func_syntax.prototype != nullptr) &&
            (func_syntax.prototype->name != nullptr)) {
          auto definition_range = func_syntax.prototype->name->sourceRange();
          CreateReference(definition_range, definition_range, subroutine);
        }
      }
    }
  }
  this->visitDefault(subroutine);
}

void SemanticIndex::IndexVisitor::handle(
    const slang::ast::DefinitionSymbol& definition) {
  if (definition.location.valid()) {
    if (const auto* syntax = definition.getSyntax()) {
      if (syntax->kind == slang::syntax::SyntaxKind::ModuleDeclaration ||
          syntax->kind == slang::syntax::SyntaxKind::InterfaceDeclaration ||
          syntax->kind == slang::syntax::SyntaxKind::ProgramDeclaration) {
        const auto& decl_syntax =
            syntax->as<slang::syntax::ModuleDeclarationSyntax>();
        auto definition_range = decl_syntax.header->name.range();
        CreateReference(definition_range, definition_range, definition);
      }
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

  // Need to traverse the target type for cases like: typedef data_from_t
  // data_to_t; This ensures we create references for data_from_t
  TraverseType(type_alias.targetType.getType());
  this->visitDefault(type_alias);
}

void SemanticIndex::IndexVisitor::handle(
    const slang::ast::EnumValueSymbol& enum_value) {
  if (enum_value.location.valid()) {
    if (const auto* syntax = enum_value.getSyntax()) {
      if (syntax->kind == slang::syntax::SyntaxKind::Declarator) {
        auto definition_range =
            syntax->as<slang::syntax::DeclaratorSyntax>().name.range();
        CreateReference(definition_range, definition_range, enum_value);
      }
    }
  }
  this->visitDefault(enum_value);
}

void SemanticIndex::IndexVisitor::handle(const slang::ast::FieldSymbol& field) {
  if (field.location.valid()) {
    if (const auto* syntax = field.getSyntax()) {
      if (syntax->kind == slang::syntax::SyntaxKind::Declarator) {
        auto definition_range =
            syntax->as<slang::syntax::DeclaratorSyntax>().name.range();
        CreateReference(definition_range, definition_range, field);
      }
    }
  }

  TraverseType(field.getType());
  this->visitDefault(field);
}

void SemanticIndex::IndexVisitor::handle(const slang::ast::NetSymbol& net) {
  if (net.location.valid()) {
    if (const auto* syntax = net.getSyntax()) {
      if (syntax->kind == slang::syntax::SyntaxKind::Declarator) {
        auto definition_range =
            syntax->as<slang::syntax::DeclaratorSyntax>().name.range();
        CreateReference(definition_range, definition_range, net);
      }
    }
  }

  TraverseType(net.getType());
  this->visitDefault(net);
}

void SemanticIndex::IndexVisitor::handle(
    const slang::ast::InterfacePortSymbol& interface_port) {
  if (interface_port.location.valid()) {
    if (const auto* syntax = interface_port.getSyntax()) {
      // Create self-reference for interface port
      slang::SourceRange definition_range = syntax->sourceRange();
      if (syntax->kind == slang::syntax::SyntaxKind::InterfacePortHeader) {
        definition_range =
            syntax->as<slang::syntax::InterfacePortHeaderSyntax>()
                .nameOrKeyword.range();
      }
      CreateReference(definition_range, definition_range, interface_port);

      // Create cross-reference from interface name to interface definition
      if (interface_port.interfaceDef != nullptr &&
          interface_port.interfaceDef->location.valid()) {
        auto interface_name_range = interface_port.interfaceNameRange();
        if (interface_name_range.start().valid()) {
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

      // Create cross-reference from modport name to modport definition
      if (!interface_port.modport.empty()) {
        auto modport_range = interface_port.modportNameRange();
        if (modport_range.start().valid()) {
          auto connection = interface_port.getConnection();
          if (connection.second != nullptr &&
              connection.second->location.valid()) {
            if (const auto* modport_syntax = connection.second->getSyntax()) {
              auto modport_definition_range =
                  DefinitionExtractor::ExtractDefinitionRange(
                      *connection.second, *modport_syntax);
              CreateReference(
                  modport_range, modport_definition_range, *connection.second);
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
      if (syntax->kind == slang::syntax::SyntaxKind::ModportItem) {
        auto definition_range =
            syntax->as<slang::syntax::ModportItemSyntax>().name.range();
        CreateReference(definition_range, definition_range, modport);
      }
    }
  }
  this->visitDefault(modport);
}

void SemanticIndex::IndexVisitor::handle(
    const slang::ast::ModportPortSymbol& modport_port) {
  if (modport_port.location.valid()) {
    if (const auto* syntax = modport_port.getSyntax()) {
      if (syntax->kind == slang::syntax::SyntaxKind::ModportNamedPort) {
        auto definition_range =
            syntax->as<slang::syntax::ModportNamedPortSyntax>().name.range();
        CreateReference(definition_range, definition_range, modport_port);
      }
    }
  }
  this->visitDefault(modport_port);
}

void SemanticIndex::IndexVisitor::handle(
    const slang::ast::GenerateBlockArraySymbol& generate_array) {
  // First, visit any inline genvars at the array level (not inside entries)
  // These are declared like: for (genvar j = 0; ...)
  for (const auto& member : generate_array.members()) {
    if (member.kind == slang::ast::SymbolKind::Genvar) {
      member.visit(*this);
    }
  }

  // Then process only the first entry to avoid duplicates
  // Generate for loops create multiple identical instances - we only need to
  // index the template
  if (!generate_array.entries.empty()) {
    const auto* first_entry = generate_array.entries[0];
    if (first_entry != nullptr) {
      first_entry->visit(*this);
    }
  }
  // NOTE: No visitDefault() - we manually control which children to visit
}

void SemanticIndex::IndexVisitor::handle(
    const slang::ast::GenerateBlockSymbol& generate_block) {
  // Create reference for generate block definition (only if explicitly named)
  if (generate_block.location.valid()) {
    if (const auto* syntax = generate_block.getSyntax()) {
      if (syntax->kind == slang::syntax::SyntaxKind::GenerateBlock) {
        const auto& gen_block =
            syntax->as<slang::syntax::GenerateBlockSyntax>();
        // Only create reference if there's an explicit name in the source code
        if (gen_block.beginName != nullptr) {
          auto definition_range = gen_block.beginName->name.range();
          CreateReference(definition_range, definition_range, generate_block);
        }
        // For unnamed blocks (auto-generated names like "genblk1"), don't
        // create reference since users can't click on text that doesn't exist
        // in source
      }
    }
  }
  this->visitDefault(generate_block);
}

void SemanticIndex::IndexVisitor::handle(
    const slang::ast::GenvarSymbol& genvar) {
  if (genvar.location.valid()) {
    if (const auto* syntax = genvar.getSyntax()) {
      // GenvarSymbol.getSyntax() returns IdentifierName - just use its range
      // The symbol itself already points to the precise genvar name location
      slang::SourceRange definition_range = syntax->sourceRange();
      CreateReference(definition_range, definition_range, genvar);
    }
  }
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

void SemanticIndex::ValidateNoRangeOverlaps() const {
  for (size_t i = 0; i < references_.size(); ++i) {
    for (size_t j = i + 1; j < references_.size(); ++j) {
      const auto& ref1 = references_[i];
      const auto& ref2 = references_[j];

      // Check if ranges overlap manually
      bool overlap =
          (ref1.source_range.start() < ref2.source_range.end() &&
           ref2.source_range.start() < ref1.source_range.end());

      if (overlap) {
        // This is a critical bug - ranges should NEVER overlap
        throw std::runtime_error(
            "Range overlap detected - fix reference creation!");
      }
    }
  }
  // Range validation passed - no overlaps detected
}

}  // namespace slangd::semantic

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

void SemanticIndex::IndexVisitor::ProcessDimensionsInScope(
    const slang::ast::Scope& scope,
    const slang::syntax::SyntaxList<slang::syntax::VariableDimensionSyntax>&
        dimensions) {
  for (const auto& dim : dimensions) {
    if (dim == nullptr || dim->specifier == nullptr) {
      continue;
    }

    const auto& spec = *dim->specifier;
    slang::ast::ASTContext context{scope, slang::ast::LookupLocation::max};

    switch (spec.kind) {
      case slang::syntax::SyntaxKind::RangeDimensionSpecifier: {
        const auto& range_spec =
            spec.as<slang::syntax::RangeDimensionSpecifierSyntax>();
        if (range_spec.selector == nullptr) {
          break;
        }

        const auto& selector = *range_spec.selector;
        switch (selector.kind) {
          case slang::syntax::SyntaxKind::BitSelect: {
            const auto& bit_select =
                selector.as<slang::syntax::BitSelectSyntax>();
            if (bit_select.expr != nullptr) {
              const auto& expr =
                  slang::ast::Expression::bind(*bit_select.expr, context);
              expr.visit(*this);
            }
            break;
          }
          case slang::syntax::SyntaxKind::SimpleRangeSelect:
          case slang::syntax::SyntaxKind::AscendingRangeSelect:
          case slang::syntax::SyntaxKind::DescendingRangeSelect: {
            const auto& range_select =
                selector.as<slang::syntax::RangeSelectSyntax>();
            if (range_select.left != nullptr) {
              const auto& left_expr =
                  slang::ast::Expression::bind(*range_select.left, context);
              left_expr.visit(*this);
            }
            if (range_select.right != nullptr) {
              const auto& right_expr =
                  slang::ast::Expression::bind(*range_select.right, context);
              right_expr.visit(*this);
            }
            break;
          }
          default:
            // Other selector kinds don't contain parameter expressions
            break;
        }
        break;
      }
      case slang::syntax::SyntaxKind::WildcardDimensionSpecifier:
        // Wildcard dimensions (e.g., [*]) don't contain expressions to visit
        break;
      case slang::syntax::SyntaxKind::QueueDimensionSpecifier: {
        const auto& queue_spec =
            spec.as<slang::syntax::QueueDimensionSpecifierSyntax>();
        if (queue_spec.maxSizeClause != nullptr &&
            queue_spec.maxSizeClause->expr != nullptr) {
          const auto& max_size_expr = slang::ast::Expression::bind(
              *queue_spec.maxSizeClause->expr, context);
          max_size_expr.visit(*this);
        }
        break;
      }
      default:
        // Other dimension specifier kinds don't contain parameter expressions
        break;
    }
  }
}

void SemanticIndex::IndexVisitor::ProcessVariableDimensions(
    const slang::ast::VariableSymbol& symbol,
    const slang::syntax::SyntaxList<slang::syntax::VariableDimensionSyntax>&
        dimensions) {
  ProcessDimensionsInScope(*symbol.getParentScope(), dimensions);
}

void SemanticIndex::IndexVisitor::ProcessIntegerTypeDimensions(
    const slang::ast::Scope& scope,
    const slang::syntax::DataTypeSyntax& type_syntax) {
  if (type_syntax.kind == slang::syntax::SyntaxKind::LogicType ||
      type_syntax.kind == slang::syntax::SyntaxKind::RegType ||
      type_syntax.kind == slang::syntax::SyntaxKind::BitType) {
    const auto& integer_type =
        type_syntax.as<slang::syntax::IntegerTypeSyntax>();
    ProcessDimensionsInScope(scope, integer_type.dimensions);
  }
}

void SemanticIndex::IndexVisitor::CreateReference(
    slang::SourceRange source_range, const slang::ast::Symbol& target_symbol) {
  if (target_symbol.location.valid()) {
    if (const auto* syntax = target_symbol.getSyntax()) {
      auto definition_range =
          DefinitionExtractor::ExtractDefinitionRange(target_symbol, *syntax);

      ReferenceEntry ref{
          .source_range = source_range,
          .target_loc = target_symbol.location,
          .target_range = definition_range,
          .symbol_kind = ConvertToLspKind(target_symbol),
          .symbol_name = std::string(target_symbol.name)};
      index_->references_.push_back(ref);
    }
  }
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

  CreateReference(expr.sourceRange, *target_symbol);
  this->visitDefault(expr);
}

void SemanticIndex::IndexVisitor::handle(
    const slang::ast::CallExpression& expr) {
  // Handle references to subroutines (functions/tasks) in calls
  if (!expr.isSystemCall()) {
    if (const auto* subroutine_symbol = std::get_if<0>(&expr.subroutine)) {
      CreateReference(expr.sourceRange, **subroutine_symbol);
    }
  }
  this->visitDefault(expr);
}

void SemanticIndex::IndexVisitor::handle(
    const slang::ast::ConversionExpression& expr) {
  CreateReference(expr.sourceRange, *expr.type);
  this->visitDefault(expr);
}

void SemanticIndex::IndexVisitor::handle(
    const slang::ast::MemberAccessExpression& expr) {
  // Create reference from member access to the field symbol
  CreateReference(expr.sourceRange, expr.member);
  this->visitDefault(expr);
}

void SemanticIndex::IndexVisitor::handle(
    const slang::ast::VariableSymbol& symbol) {
  const auto& declared_type = symbol.getDeclaredType();
  if (const auto& type_syntax = declared_type->getTypeSyntax()) {
    if (type_syntax->kind == slang::syntax::SyntaxKind::NamedType) {
      const auto& named_type =
          type_syntax->as<slang::syntax::NamedTypeSyntax>();
      const auto& resolved_type = symbol.getType();
      CreateReference(named_type.name->sourceRange(), resolved_type);
    }

    ProcessIntegerTypeDimensions(*symbol.getParentScope(), *type_syntax);
  }

  if (const auto* decl_syntax = symbol.getSyntax()) {
    if (decl_syntax->kind == slang::syntax::SyntaxKind::Declarator) {
      const auto& declarator =
          decl_syntax->as<slang::syntax::DeclaratorSyntax>();
      ProcessVariableDimensions(symbol, declarator.dimensions);
    }
  }

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

  CreateReference(import_item.package.range(), *package);
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

  CreateReference(import_item.package.range(), *package);

  // Create reference for the imported symbol name
  const auto* imported_symbol = import_symbol.importedSymbol();
  if (imported_symbol != nullptr) {
    CreateReference(import_item.item.range(), *imported_symbol);
  }

  this->visitDefault(import_symbol);
}

void SemanticIndex::IndexVisitor::handle(
    const slang::ast::ParameterSymbol& param) {
  if (param.location.valid()) {
    if (const auto* syntax = param.getSyntax()) {
      auto definition_range =
          DefinitionExtractor::ExtractDefinitionRange(param, *syntax);
      CreateReference(definition_range, param);
    }
  }
  this->visitDefault(param);
}

void SemanticIndex::IndexVisitor::handle(
    const slang::ast::SubroutineSymbol& subroutine) {
  if (subroutine.location.valid()) {
    if (const auto* syntax = subroutine.getSyntax()) {
      auto definition_range =
          DefinitionExtractor::ExtractDefinitionRange(subroutine, *syntax);
      CreateReference(definition_range, subroutine);
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
      CreateReference(definition_range, definition);
    }
  }
  this->visitDefault(definition);
}

void SemanticIndex::IndexVisitor::handle(
    const slang::ast::TypeAliasType& type_alias) {
  if (const auto* typedef_syntax = type_alias.getSyntax()) {
    if (typedef_syntax->kind == slang::syntax::SyntaxKind::TypedefDeclaration) {
      const auto& typedef_decl =
          typedef_syntax->as<slang::syntax::TypedefDeclarationSyntax>();
      ProcessDimensionsInScope(
          *type_alias.getParentScope(), typedef_decl.dimensions);
    }
  }

  if (type_alias.location.valid()) {
    if (const auto* syntax = type_alias.getSyntax()) {
      auto definition_range =
          DefinitionExtractor::ExtractDefinitionRange(type_alias, *syntax);
      CreateReference(definition_range, type_alias);
    }
  }

  if (const auto* target_syntax = type_alias.targetType.getTypeSyntax()) {
    ProcessIntegerTypeDimensions(*type_alias.getParentScope(), *target_syntax);
  }

  this->visitDefault(type_alias);
}

void SemanticIndex::IndexVisitor::handle(
    const slang::ast::EnumValueSymbol& enum_value) {
  if (enum_value.location.valid()) {
    if (const auto* syntax = enum_value.getSyntax()) {
      auto definition_range =
          DefinitionExtractor::ExtractDefinitionRange(enum_value, *syntax);
      CreateReference(definition_range, enum_value);
    }
  }
  this->visitDefault(enum_value);
}

void SemanticIndex::IndexVisitor::handle(const slang::ast::FieldSymbol& field) {
  if (field.location.valid()) {
    if (const auto* syntax = field.getSyntax()) {
      auto definition_range =
          DefinitionExtractor::ExtractDefinitionRange(field, *syntax);
      CreateReference(definition_range, field);
    }
  }
  this->visitDefault(field);
}

void SemanticIndex::IndexVisitor::handle(const slang::ast::NetSymbol& net) {
  if (net.location.valid()) {
    if (const auto* syntax = net.getSyntax()) {
      auto definition_range =
          DefinitionExtractor::ExtractDefinitionRange(net, *syntax);
      CreateReference(definition_range, net);
    }
  }
  this->visitDefault(net);
}

// Go-to-definition implementation
auto SemanticIndex::LookupDefinitionAt(slang::SourceLocation loc) const
    -> std::optional<slang::SourceRange> {
  // Direct lookup using unified reference storage
  // Linear search through references for position containment
  for (const auto& ref_entry : references_) {
    if (ref_entry.source_range.contains(loc)) {
      return ref_entry.target_range;
    }
  }

  return std::nullopt;
}

}  // namespace slangd::semantic

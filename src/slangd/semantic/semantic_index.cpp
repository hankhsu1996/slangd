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
#include <slang/ast/symbols/ClassSymbols.h>
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
#include "slangd/services/global_catalog.hpp"
#include "slangd/utils/conversion.hpp"

namespace slangd::semantic {

auto SemanticIndex::FromCompilation(
    slang::ast::Compilation& compilation,
    const slang::SourceManager& source_manager,
    const std::string& current_file_uri, const services::GlobalCatalog* catalog)
    -> std::unique_ptr<SemanticIndex> {
  auto index =
      std::unique_ptr<SemanticIndex>(new SemanticIndex(source_manager));

  // Create visitor for comprehensive symbol collection and reference tracking
  auto visitor =
      IndexVisitor(*index, source_manager, current_file_uri, catalog);

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

  // Sort entries by source location for O(n) validation and potential lookup
  // optimizations O(n log n) - trivially fast even for 100k entries
  std::sort(
      index->semantic_entries_.begin(), index->semantic_entries_.end(),
      [](const SemanticEntry& a, const SemanticEntry& b) -> bool {
        // Sort by buffer ID first, then by offset
        if (a.source_range.start().buffer().getId() !=
            b.source_range.start().buffer().getId()) {
          return a.source_range.start().buffer().getId() <
                 b.source_range.start().buffer().getId();
        }
        return a.source_range.start().offset() <
               b.source_range.start().offset();
      });

  // Validate no overlaps using O(n) algorithm (entries are now sorted)
  index->ValidateNoRangeOverlaps();

  return index;
}

auto SemanticEntry::Make(
    const slang::ast::Symbol& symbol, std::string_view name,
    slang::SourceRange source_range, bool is_definition,
    slang::SourceRange definition_range, const slang::ast::Scope* parent_scope)
    -> SemanticEntry {
  // Unwrap symbol to handle TransparentMember recursion
  const auto& unwrapped = UnwrapSymbol(symbol);

  return SemanticEntry{
      .source_range = source_range,
      .location = unwrapped.location,
      .symbol = &unwrapped,
      .lsp_kind = ConvertToLspKind(unwrapped),
      .name = std::string(name),
      .parent = parent_scope,
      .is_definition = is_definition,
      .definition_range = definition_range,
      .cross_file_path = std::nullopt,
      .cross_file_range = std::nullopt,
      .buffer_id = unwrapped.location.buffer()};
}

auto SemanticIndex::GetDocumentSymbols(const std::string& uri) const
    -> std::vector<lsp::DocumentSymbol> {
  return DocumentSymbolBuilder::BuildDocumentSymbolTree(uri, *this);
}

// IndexVisitor helper methods
void SemanticIndex::IndexVisitor::AddEntry(SemanticEntry entry) {
  index_.get().semantic_entries_.push_back(std::move(entry));
}

void SemanticIndex::IndexVisitor::AddDefinition(
    const slang::ast::Symbol& symbol, std::string_view name,
    slang::SourceRange range, const slang::ast::Scope* parent_scope) {
  AddEntry(SemanticEntry::Make(symbol, name, range, true, range, parent_scope));
}

void SemanticIndex::IndexVisitor::AddReference(
    const slang::ast::Symbol& symbol, std::string_view name,
    slang::SourceRange source_range, slang::SourceRange definition_range,
    const slang::ast::Scope* parent_scope) {
  AddEntry(
      SemanticEntry::Make(
          symbol, name, source_range, false, definition_range, parent_scope));
}

void SemanticIndex::IndexVisitor::AddCrossFileReference(
    const slang::ast::Symbol& symbol, std::string_view name,
    slang::SourceRange source_range, slang::SourceRange definition_range,
    const slang::SourceManager& catalog_source_manager,
    const slang::ast::Scope* parent_scope) {
  // Create base entry
  auto entry = SemanticEntry::Make(
      symbol, name, source_range, false, definition_range, parent_scope);

  // Convert definition_range to compilation-independent format using catalog's
  // source manager
  auto file_name = catalog_source_manager.getFileName(definition_range.start());
  entry.cross_file_path = CanonicalPath(std::filesystem::path(file_name));
  entry.cross_file_range =
      ConvertSlangRangeToLspRange(definition_range, catalog_source_manager);

  AddEntry(std::move(entry));
}

// IndexVisitor implementation
void SemanticIndex::IndexVisitor::TraverseType(const slang::ast::Type& type) {
  // Skip if already traversed - multiple symbols can share the same type syntax
  if (const auto* type_syntax = type.getSyntax()) {
    if (!visited_type_syntaxes_.insert(type_syntax).second) {
      return;
    }
  }

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
      const auto& resolved_type = type_ref.getResolvedType();

      if (const auto* typedef_target =
              resolved_type.as_if<slang::ast::TypeAliasType>()) {
        if (typedef_target->location.valid()) {
          if (const auto* syntax = typedef_target->getSyntax()) {
            if (syntax->kind == slang::syntax::SyntaxKind::TypedefDeclaration) {
              auto definition_range =
                  syntax->as<slang::syntax::TypedefDeclarationSyntax>()
                      .name.range();
              AddReference(
                  *typedef_target, typedef_target->name,
                  type_ref.getUsageLocation(), definition_range,
                  typedef_target->getParentScope());
            }
          }
        }
      } else if (
          const auto* class_target =
              resolved_type.as_if<slang::ast::ClassType>()) {
        if (class_target->location.valid()) {
          if (const auto* syntax = class_target->getSyntax()) {
            if (syntax->kind == slang::syntax::SyntaxKind::ClassDeclaration) {
              auto definition_range =
                  syntax->as<slang::syntax::ClassDeclarationSyntax>()
                      .name.range();
              AddReference(
                  *class_target, class_target->name,
                  type_ref.getUsageLocation(), definition_range,
                  class_target->getParentScope());
            }
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
    case slang::ast::SymbolKind::ClassType: {
      // ClassType references are handled via TypeReference wrapping
      // For now, we just skip traversal to avoid duplicate indexing
      break;
    }
    default:
      break;
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

  // Handle compiler-generated variables
  if (expr.symbol.kind == slang::ast::SymbolKind::Variable) {
    const auto& variable = expr.symbol.as<slang::ast::VariableSymbol>();
    if (variable.flags.has(slang::ast::VariableFlags::CompilerGenerated)) {
      // Slang provides declaredSymbol pointer for compiler-generated variables
      // (e.g., genvar loop iteration variables point to the actual genvar)
      if (const auto* declared = variable.getDeclaredSymbol()) {
        target_symbol = declared;
      }
      // Fallback: function return variables redirect to parent subroutine
      else {
        const auto* parent_scope = variable.getParentScope();
        if (parent_scope != nullptr) {
          const auto& parent_symbol = parent_scope->asSymbol();
          if (parent_symbol.kind == slang::ast::SymbolKind::Subroutine) {
            target_symbol = &parent_symbol;
          }
        }
      }
    }
  }

  if (target_symbol->location.valid()) {
    if (const auto* syntax = target_symbol->getSyntax()) {
      using SK = slang::ast::SymbolKind;
      using SyntaxKind = slang::syntax::SyntaxKind;

      // Extract precise definition range based on symbol and syntax type
      slang::SourceRange definition_range;
      bool range_extracted = false;

      switch (target_symbol->kind) {
        case SK::Parameter:
          // Parameter/localparam declarators
          if (syntax->kind == SyntaxKind::Declarator) {
            definition_range =
                syntax->as<slang::syntax::DeclaratorSyntax>().name.range();
            range_extracted = true;
          }
          break;

        case SK::EnumValue:
          // Enum member declarators
          if (syntax->kind == SyntaxKind::Declarator) {
            definition_range =
                syntax->as<slang::syntax::DeclaratorSyntax>().name.range();
            range_extracted = true;
          }
          break;

        case SK::Subroutine:
          // Function/task declarations
          if (syntax->kind == SyntaxKind::TaskDeclaration ||
              syntax->kind == SyntaxKind::FunctionDeclaration) {
            const auto& func_syntax =
                syntax->as<slang::syntax::FunctionDeclarationSyntax>();
            if ((func_syntax.prototype != nullptr) &&
                (func_syntax.prototype->name != nullptr)) {
              definition_range = func_syntax.prototype->name->sourceRange();
              range_extracted = true;
            }
          }
          break;

        case SK::StatementBlock:
          // Named statement blocks (begin/end)
          if (syntax->kind == SyntaxKind::SequentialBlockStatement ||
              syntax->kind == SyntaxKind::ParallelBlockStatement) {
            const auto& block_syntax =
                syntax->as<slang::syntax::BlockStatementSyntax>();
            if (block_syntax.blockName != nullptr) {
              definition_range = block_syntax.blockName->name.range();
              range_extracted = true;
            }
          }
          break;

        default:
          // For other symbol kinds, use fallback
          break;
      }

      // Fallback: use symbol location + name length
      if (!range_extracted) {
        if (target_symbol->location.valid()) {
          definition_range = slang::SourceRange(
              target_symbol->location,
              target_symbol->location + target_symbol->name.length());
        } else {
          // Should never reach here - symbol with syntax but no valid location
          spdlog::error(
              "NamedValueExpression: Symbol '{}' (kind '{}') has syntax but "
              "invalid location",
              target_symbol->name, slang::ast::toString(target_symbol->kind));
          definition_range = syntax->sourceRange();
        }
      }

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

      AddReference(
          *target_symbol, target_symbol->name, reference_range,
          definition_range, target_symbol->getParentScope());
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

  // Helper to index class specialization (e.g., Class#(.PARAM(value)))
  auto index_scoped_names = [&](const slang::syntax::NameSyntax* name) -> void {
    // TODO: Traverse scoped names to index ClassName and parameter references
    // For now, this is a placeholder for future implementation
    (void)name;
  };

  auto extract_call_range = [&]() -> std::optional<slang::SourceRange> {
    if (expr.syntax == nullptr) {
      return std::nullopt;
    }

    if (expr.syntax->kind == slang::syntax::SyntaxKind::InvocationExpression) {
      const auto& invocation =
          expr.syntax->as<slang::syntax::InvocationExpressionSyntax>();

      // For ScopedName (e.g., pkg::Class#(...)::func), extract the rightmost
      // name to get precise function name range, not the entire scope chain
      if (invocation.left->kind == slang::syntax::SyntaxKind::ScopedName) {
        const auto& scoped =
            invocation.left->as<slang::syntax::ScopedNameSyntax>();

        // Index any class specializations in the scoped chain
        index_scoped_names(&scoped);

        return scoped.right->sourceRange();
      }

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

  AddReference(
      **subroutine_symbol, (*subroutine_symbol)->name, *call_range,
      *definition_range, (*subroutine_symbol)->getParentScope());
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
      AddReference(
          expr.member, expr.member.name, expr.memberNameRange(),
          definition_range, expr.member.getParentScope());
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

      AddReference(
          *target_symbol, target_symbol->name, reference_range,
          definition_range, target_symbol->getParentScope());
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
          AddDefinition(
              symbol, symbol.name, definition_range, symbol.getParentScope());
          break;
        }
      }
      break;
    }
    case slang::syntax::SyntaxKind::ForVariableDeclaration:
    case slang::syntax::SyntaxKind::CheckerDataDeclaration: {
      auto definition_range = syntax->sourceRange();
      AddDefinition(
          symbol, symbol.name, definition_range, symbol.getParentScope());
      break;
    }
    case slang::syntax::SyntaxKind::Declarator: {
      const auto& decl_syntax = syntax->as<slang::syntax::DeclaratorSyntax>();
      auto definition_range = decl_syntax.name.range();
      AddDefinition(
          symbol, symbol.name, definition_range, symbol.getParentScope());
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
  AddReference(
      *package, package->name, import_item.package.range(), definition_range,
      package->getParentScope());
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
  AddReference(
      *package, package->name, import_item.package.range(), definition_range,
      package->getParentScope());

  // Create entry for the imported symbol name
  const auto* imported_symbol = import_symbol.importedSymbol();
  if (imported_symbol != nullptr) {
    if (imported_symbol->location.valid()) {
      if (const auto* imported_syntax = imported_symbol->getSyntax()) {
        auto imported_definition_range =
            DefinitionExtractor::ExtractDefinitionRange(
                *imported_symbol, *imported_syntax);
        AddReference(
            *imported_symbol, imported_symbol->name, import_item.item.range(),
            imported_definition_range, imported_symbol->getParentScope());
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
        AddDefinition(
            param, param.name, definition_range, param.getParentScope());
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
          AddDefinition(
              subroutine, subroutine.name, definition_range,
              subroutine.getParentScope());
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
        AddDefinition(
            definition, definition.name, definition_range,
            definition.getParentScope());
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
        AddDefinition(
            type_alias, type_alias.name, definition_range,
            type_alias.getParentScope());
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
        AddDefinition(
            enum_value, enum_value.name, definition_range,
            enum_value.getParentScope());
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
        AddDefinition(
            field, field.name, definition_range, field.getParentScope());
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
        AddDefinition(net, net.name, definition_range, net.getParentScope());
      }
    }
  }

  TraverseType(net.getType());
  this->visitDefault(net);
}

void SemanticIndex::IndexVisitor::handle(
    const slang::ast::GenericClassDefSymbol& class_def) {
  if (class_def.location.valid()) {
    if (const auto* syntax = class_def.getSyntax()) {
      if (syntax->kind == slang::syntax::SyntaxKind::ClassDeclaration) {
        auto definition_range =
            syntax->as<slang::syntax::ClassDeclarationSyntax>().name.range();
        AddDefinition(
            class_def, class_def.name, definition_range,
            class_def.getParentScope());
      }
    }
  }
  this->visitDefault(class_def);
}

void SemanticIndex::IndexVisitor::handle(
    const slang::ast::ClassType& class_type) {
  if (class_type.location.valid()) {
    if (const auto* syntax = class_type.getSyntax()) {
      if (syntax->kind == slang::syntax::SyntaxKind::ClassDeclaration) {
        auto definition_range =
            syntax->as<slang::syntax::ClassDeclarationSyntax>().name.range();
        AddDefinition(
            class_type, class_type.name, definition_range,
            class_type.getParentScope());
      }
    }
  }
  this->visitDefault(class_type);
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
      AddDefinition(
          interface_port, interface_port.name, definition_range,
          interface_port.getParentScope());

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
            AddReference(
                *interface_port.interfaceDef, interface_port.interfaceDef->name,
                interface_name_range, interface_definition_range,
                interface_port.interfaceDef->getParentScope());
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
              AddReference(
                  *connection.second, connection.second->name, modport_range,
                  modport_definition_range,
                  connection.second->getParentScope());
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
        AddDefinition(
            modport, modport.name, definition_range, modport.getParentScope());
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
        AddDefinition(
            modport_port, modport_port.name, definition_range,
            modport_port.getParentScope());
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

  // Visit loop control expressions (initialization, condition, increment)
  // For example: for (genvar i = INIT; i < NUM; i++) has references to INIT,
  // NUM
  if (generate_array.initialExpression != nullptr) {
    generate_array.initialExpression->visit(*this);
  }
  if (generate_array.stopExpression != nullptr) {
    generate_array.stopExpression->visit(*this);
  }
  if (generate_array.iterExpression != nullptr) {
    generate_array.iterExpression->visit(*this);
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
          auto name_text = gen_block.beginName->name.valueText();

          // Skip GenerateBlockArray parent since it's not indexed in document
          // symbols
          const slang::ast::Scope* parent_scope =
              generate_block.getParentScope();
          if (parent_scope != nullptr &&
              parent_scope->asSymbol().kind ==
                  slang::ast::SymbolKind::GenerateBlockArray) {
            parent_scope = parent_scope->asSymbol().getParentScope();
          }

          AddDefinition(
              generate_block, name_text, definition_range, parent_scope);
        }
        // For unnamed blocks (auto-generated names like "genblk1"), don't
        // create reference since users can't click on text that doesn't exist
        // in source
      }
    }
  }

  // Visit condition expression for if/case generate blocks
  // For example: if (ENABLE) has a reference to ENABLE parameter
  // Multiple sibling blocks (if/else branches, case branches) share the same
  // condition pointer, so we deduplicate to avoid visiting it multiple times
  if (generate_block.conditionExpression != nullptr) {
    auto [_, inserted] =
        visited_generate_conditions_.insert(generate_block.conditionExpression);
    if (inserted) {
      generate_block.conditionExpression->visit(*this);
    }
  }

  // Visit case item expressions for case generate blocks
  // For example: case (MODE) MODE_A: has a reference to MODE_A parameter
  for (const auto* item_expr : generate_block.caseItemExpressions) {
    if (item_expr != nullptr) {
      item_expr->visit(*this);
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
      AddDefinition(
          genvar, genvar.name, definition_range, genvar.getParentScope());
    }
  }
}

void SemanticIndex::IndexVisitor::handle(
    const slang::ast::PackageSymbol& package) {
  if (package.location.valid()) {
    if (const auto* syntax = package.getSyntax()) {
      if (syntax->kind == slang::syntax::SyntaxKind::PackageDeclaration) {
        const auto& decl_syntax =
            syntax->as<slang::syntax::ModuleDeclarationSyntax>();
        auto definition_range = decl_syntax.header->name.range();
        AddDefinition(
            package, package.name, definition_range, package.getParentScope());
      }
    }
  }
  this->visitDefault(package);
}

void SemanticIndex::IndexVisitor::handle(
    const slang::ast::StatementBlockSymbol& statement_block) {
  // StatementBlockSymbol represents named statement blocks (e.g., assertion
  // labels) Only index if it has a valid name (not empty or auto-generated)
  if (statement_block.location.valid() && !statement_block.name.empty()) {
    auto definition_range =
        slang::SourceRange{statement_block.location, statement_block.location};

    AddDefinition(
        statement_block, statement_block.name, definition_range,
        statement_block.getParentScope());
  }
  this->visitDefault(statement_block);
}

void SemanticIndex::IndexVisitor::handle(
    const slang::ast::UninstantiatedDefSymbol& symbol) {
  const auto* syntax = symbol.getSyntax();
  if (syntax == nullptr) {
    this->visitDefault(symbol);
    return;
  }

  // Always create self-definition for instance name (same-file and cross-file)
  if (symbol.location.valid() &&
      syntax->kind == slang::syntax::SyntaxKind::HierarchicalInstance) {
    auto start_loc = symbol.location;
    auto end_loc = start_loc + symbol.name.length();
    auto name_range = slang::SourceRange{start_loc, end_loc};
    AddDefinition(symbol, symbol.name, name_range, symbol.getParentScope());
  }

  // Visit parameter and port expressions (for same-file cases)
  // UninstantiatedDefSymbol stores these expressions even without catalog
  for (const auto* expr : symbol.paramExpressions) {
    if (expr != nullptr) {
      expr->visit(*this);
    }
  }

  auto port_conns = symbol.getPortConnections();
  for (const auto* port_conn : port_conns) {
    if (port_conn != nullptr) {
      port_conn->visit(*this);
    }
  }

  // Cross-file handling requires catalog
  if (catalog_ == nullptr) {
    this->visitDefault(symbol);
    return;
  }

  const auto* module_info = catalog_->GetModule(symbol.definitionName);
  if (module_info == nullptr) {
    this->visitDefault(symbol);
    return;
  }

  // The syntax is HierarchicalInstanceSyntax, whose parent is
  // HierarchyInstantiationSyntax We need to get the parent to access the type
  // name range
  if (syntax->kind == slang::syntax::SyntaxKind::HierarchicalInstance) {
    const auto* parent_syntax = syntax->parent;
    if (parent_syntax != nullptr &&
        parent_syntax->kind ==
            slang::syntax::SyntaxKind::HierarchyInstantiation) {
      const auto& inst_syntax =
          parent_syntax->as<slang::syntax::HierarchyInstantiationSyntax>();
      auto type_range = inst_syntax.type.range();

      // Module definitions are in GlobalCatalog's compilation, not
      // OverlaySession Use AddCrossFileReference to store
      // compilation-independent location
      const auto& catalog_sm = catalog_->GetSourceManager();
      AddCrossFileReference(
          symbol, symbol.definitionName, type_range,
          module_info->definition_range, catalog_sm, symbol.getParentScope());

      // Handle port connections (named ports only, skip positional)
      const auto& hier_inst_syntax =
          syntax->as<slang::syntax::HierarchicalInstanceSyntax>();
      for (const auto* port_conn : hier_inst_syntax.connections) {
        if (port_conn->kind == slang::syntax::SyntaxKind::NamedPortConnection) {
          const auto& npc =
              port_conn->as<slang::syntax::NamedPortConnectionSyntax>();
          std::string_view port_name = npc.name.valueText();

          // O(1) lookup in port hash map
          auto it = module_info->port_lookup.find(std::string(port_name));
          if (it != module_info->port_lookup.end()) {
            const auto* port_info = it->second;
            AddCrossFileReference(
                symbol, port_name, npc.name.range(), port_info->def_range,
                catalog_sm, symbol.getParentScope());
          }
        }
      }

      // Handle parameter assignments (named parameters only)
      if (inst_syntax.parameters != nullptr) {
        const auto& param_assign = *inst_syntax.parameters;
        for (const auto* param : param_assign.parameters) {
          if (param->kind == slang::syntax::SyntaxKind::NamedParamAssignment) {
            const auto& npa =
                param->as<slang::syntax::NamedParamAssignmentSyntax>();
            std::string_view param_name = npa.name.valueText();

            // O(1) lookup in parameter hash map
            auto it =
                module_info->parameter_lookup.find(std::string(param_name));
            if (it != module_info->parameter_lookup.end()) {
              const auto* param_info = it->second;
              AddCrossFileReference(
                  symbol, param_name, npa.name.range(), param_info->def_range,
                  catalog_sm, symbol.getParentScope());
            }
          }
        }
      }
    }
  }

  this->visitDefault(symbol);
}

// Go-to-definition implementation
auto SemanticIndex::LookupDefinitionAt(slang::SourceLocation loc) const
    -> std::optional<DefinitionLocation> {
  // Binary search in sorted entries by (buffer_id, offset)
  const auto target = std::pair{loc.buffer().getId(), loc.offset()};

  const auto proj = [](const SemanticEntry& e) -> std::pair<unsigned, size_t> {
    return std::pair{
        e.source_range.start().buffer().getId(),
        e.source_range.start().offset()};
  };

  // upper_bound returns the first entry whose start is AFTER target location
  auto it = std::ranges::upper_bound(
      semantic_entries_, target, std::ranges::less{}, proj);

  // Move back one entry - this is the candidate that might contain our location
  // (since its start is <= target, but the next entry's start is > target)
  if (it != semantic_entries_.begin()) {
    --it;
    // Check if the candidate entry actually contains the target location
    if (it->source_range.contains(loc)) {
      DefinitionLocation def_loc;

      // Check if this is a cross-file reference (has cross_file_path set)
      if (it->cross_file_path.has_value()) {
        def_loc.cross_file_path = it->cross_file_path;
        def_loc.cross_file_range = it->cross_file_range;
      } else {
        def_loc.same_file_range = it->definition_range;
      }

      return def_loc;
    }
  }

  return std::nullopt;
}

void SemanticIndex::ValidateNoRangeOverlaps() const {
  if (semantic_entries_.empty()) {
    return;
  }

  // O(n) validation - entries are pre-sorted, so we only check adjacent pairs
  // This is much faster than O(n²) and catches all overlaps in sorted data
  for (size_t i = 1; i < semantic_entries_.size(); ++i) {
    const auto& prev = semantic_entries_[i - 1];
    const auto& curr = semantic_entries_[i];

    // Check if current overlaps with previous (they should be disjoint)
    // Two ranges [a,b) and [c,d) overlap if: a < d && c < b
    bool overlap =
        (prev.source_range.start() < curr.source_range.end() &&
         curr.source_range.start() < prev.source_range.end());

    if (overlap) {
      // Log error but don't crash - LSP server should continue working
      auto prev_loc = prev.source_range.start();
      auto curr_loc = curr.source_range.start();
      spdlog::error(
          "Range overlap detected: prev=[{}:{}..{}:{}] '{}', "
          "curr=[{}:{}..{}:{}] '{}'. Please report this bug.",
          prev_loc.buffer().getId(), prev_loc.offset(),
          prev.source_range.end().buffer().getId(),
          prev.source_range.end().offset(), prev.name,
          curr_loc.buffer().getId(), curr_loc.offset(),
          curr.source_range.end().buffer().getId(),
          curr.source_range.end().offset(), curr.name);
      // Don't throw in production - continue processing
    }
  }
  // Range validation passed - no overlaps detected
}

}  // namespace slangd::semantic

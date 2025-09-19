#include "slangd/semantic/semantic_index.hpp"

#include <filesystem>

#include <slang/ast/Compilation.h>
#include <slang/ast/Symbol.h>
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

  // Definition ranges are now stored in references_ vector when references are
  // collected
}

void SemanticIndex::IndexVisitor::handle(
    const slang::ast::NamedValueExpression& expr) {
  // Store reference with embedded definition information
  if (expr.symbol.location.valid()) {
    if (const auto* syntax = expr.symbol.getSyntax()) {
      auto definition_range =
          DefinitionExtractor::ExtractDefinitionRange(expr.symbol, *syntax);

      // Unified references_ storage with embedded definition range
      ReferenceEntry ref_entry{
          .source_range = expr.sourceRange,
          .target_loc = expr.symbol.location,
          .target_range = definition_range,
          .symbol_kind = ConvertToLspKind(expr.symbol),
          .symbol_name = std::string(expr.symbol.name)};
      index_->references_.push_back(ref_entry);
    }
  }

  // Continue traversal
  this->visitDefault(expr);
}

void SemanticIndex::IndexVisitor::handle(
    const slang::ast::VariableSymbol& symbol) {
  // Track type references in variable declarations (e.g., data_t my_data;)
  const auto& declared_type = symbol.getDeclaredType();
  if (const auto& type_syntax = declared_type->getTypeSyntax()) {
    if (type_syntax->kind == slang::syntax::SyntaxKind::NamedType) {
      const auto& named_type =
          type_syntax->as<slang::syntax::NamedTypeSyntax>();
      const auto& resolved_type = symbol.getType();

      if (resolved_type.location.valid()) {
        // Store type reference with embedded definition range
        if (const auto* syntax = resolved_type.getSyntax()) {
          auto definition_range = DefinitionExtractor::ExtractDefinitionRange(
              resolved_type, *syntax);

          // Unified references_ storage for type references
          ReferenceEntry ref_entry{
              .source_range = named_type.name->sourceRange(),
              .target_loc = resolved_type.location,
              .target_range = definition_range,
              .symbol_kind = ConvertToLspKind(resolved_type),
              .symbol_name = std::string(resolved_type.name)};
          index_->references_.push_back(ref_entry);
        }
      }
    }
  }

  // Continue traversal
  this->visitDefault(symbol);
}

void SemanticIndex::IndexVisitor::handle(
    const slang::ast::WildcardImportSymbol& import_symbol) {
  // Track reference: import statement -> package location
  const auto* package = import_symbol.getPackage();
  if (package != nullptr && package->location.valid()) {
    // The import symbol itself doesn't have a meaningful source range for the
    // package name. We need to extract the package name reference from the
    // syntax. For now, use the import symbol's location as the reference
    // location.
    // TODO(hankhsu): Extract precise package name location from import syntax
    slang::SourceRange import_range{
        import_symbol.location, import_symbol.location};

    // Store import reference with embedded definition range
    if (const auto* pkg_syntax = package->getSyntax()) {
      auto definition_range =
          DefinitionExtractor::ExtractDefinitionRange(*package, *pkg_syntax);

      // Unified references_ storage for import references
      ReferenceEntry ref_entry{
          .source_range = import_range,
          .target_loc = package->location,
          .target_range = definition_range,
          .symbol_kind = ConvertToLspKind(*package),
          .symbol_name = std::string(package->name)};
      index_->references_.push_back(ref_entry);
    }
  }

  // Continue traversal
  this->visitDefault(import_symbol);
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

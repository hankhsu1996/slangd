#include "slangd/semantic/semantic_index.hpp"

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

#include "slangd/semantic/definition_extractor.hpp"
#include "slangd/semantic/document_symbol_builder.hpp"
#include "slangd/semantic/symbol_utils.hpp"

namespace slangd::semantic {

auto SemanticIndex::FromCompilation(
    slang::ast::Compilation& compilation,
    const slang::SourceManager& source_manager)
    -> std::unique_ptr<SemanticIndex> {
  auto index = std::unique_ptr<SemanticIndex>(new SemanticIndex());

  // Store source manager reference for document symbol processing
  index->source_manager_ = &source_manager;

  // Create visitor for comprehensive symbol collection and reference tracking
  auto visitor = IndexVisitor(index.get(), &source_manager);

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
      // New symbol is named, existing is unnamed - replace
      should_store = true;
    } else if (unwrapped.name.empty() && !existing_info.symbol->name.empty()) {
      // New symbol is unnamed, existing is named - keep existing
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
      // Keep existing GenerateBlockArray over new GenerateBlock
      should_store = false;
    }
  }

  if (should_store) {
    index_->symbols_[unwrapped.location] = info;
  }

  // Store definition range for go-to-definition API
  if (is_definition) {
    SymbolKey key = SymbolKey::FromSourceLocation(unwrapped.location);
    index_->definition_ranges_[key] = definition_range;
  }
}

void SemanticIndex::IndexVisitor::handle(
    const slang::ast::NamedValueExpression& expr) {
  // Track reference: expr.sourceRange -> expr.symbol.location
  if (expr.symbol.location.valid()) {
    SymbolKey key = SymbolKey::FromSourceLocation(expr.symbol.location);
    index_->reference_map_[expr.sourceRange] = key;
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
        SymbolKey type_key =
            SymbolKey::FromSourceLocation(resolved_type.location);
        index_->reference_map_[named_type.name->sourceRange()] = type_key;
      }
    }
  }

  // Continue traversal
  this->visitDefault(symbol);
}

auto SemanticIndex::GetDefinitionRange(const SymbolKey& key) const
    -> std::optional<slang::SourceRange> {
  auto it = definition_ranges_.find(key);
  if (it != definition_ranges_.end()) {
    return it->second;
  }
  return std::nullopt;
}

auto SemanticIndex::LookupSymbolAt(slang::SourceLocation loc) const
    -> std::optional<SymbolKey> {
  // O(n) search through reference map - matches legacy DefinitionIndex behavior
  for (const auto& [range, key] : reference_map_) {
    if (range.contains(loc)) {
      return key;
    }
  }
  return std::nullopt;
}

}  // namespace slangd::semantic

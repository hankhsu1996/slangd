#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <lsp/document_features.hpp>
#include <slang/ast/Symbol.h>
#include <slang/text/SourceManager.h>

#include "slangd/semantic/semantic_index.hpp"

namespace slangd::semantic {

// DocumentSymbolBuilder handles hierarchical tree construction for LSP document
// symbols. It processes flat symbol collections and builds nested
// DocumentSymbol trees according to scope relationships and LSP conventions.
class DocumentSymbolBuilder {
 public:
  // Main tree building method that filters symbols by URI and constructs
  // hierarchy - takes SemanticIndex directly to avoid header dependencies
  static auto BuildDocumentSymbolTree(
      const std::string& uri, const SemanticIndex& semantic_index)
      -> std::vector<lsp::DocumentSymbol>;

 private:
  // Create DocumentSymbol from SemanticEntry (returns nullopt if name is empty)
  static auto CreateDocumentSymbol(
      const SemanticEntry& entry, const slang::SourceManager& source_manager)
      -> std::optional<lsp::DocumentSymbol>;

  // Recursive attachment of child symbols to parent DocumentSymbol
  static auto AttachChildrenToSymbol(
      lsp::DocumentSymbol& parent, const slang::ast::Scope* parent_scope,
      const std::unordered_map<
          const slang::ast::Scope*, std::vector<const SemanticEntry*>>&
          children_map,
      const slang::SourceManager& source_manager) -> void;

  // Special handling for enum type aliases - extract enum values
  static auto HandleEnumTypeAlias(
      lsp::DocumentSymbol& enum_doc_symbol,
      const slang::ast::Symbol* type_alias_symbol,
      const slang::SourceManager& source_manager) -> void;

  // Special handling for struct type aliases - extract struct fields
  static auto HandleStructTypeAlias(
      lsp::DocumentSymbol& struct_doc_symbol,
      const slang::ast::Symbol* type_alias_symbol,
      const slang::SourceManager& source_manager) -> void;

  // Filter out empty generate blocks to reduce symbol tree clutter
  static auto FilterEmptyGenerateBlocks(
      std::vector<lsp::DocumentSymbol>& symbols,
      const SemanticIndex& semantic_index) -> void;
};

}  // namespace slangd::semantic

#pragma once

#include <memory>
#include <optional>
#include <unordered_map>

#include <lsp/document_features.hpp>
#include <slang/ast/ASTVisitor.h>
#include <slang/ast/Compilation.h>
#include <slang/ast/Symbol.h>
#include <slang/text/SourceLocation.h>
#include <slang/text/SourceManager.h>

namespace slangd::semantic {

// Uniquely identifies a symbol by its declaration location
struct SymbolKey {
  uint32_t bufferId;
  size_t offset;

  auto operator==(const SymbolKey&) const -> bool = default;

  // Factory method to create from SourceLocation
  static auto FromSourceLocation(const slang::SourceLocation& loc)
      -> SymbolKey {
    return SymbolKey{.bufferId = loc.buffer().getId(), .offset = loc.offset()};
  }
};

}  // namespace slangd::semantic

namespace std {
template <>
struct hash<slangd::semantic::SymbolKey> {
  auto operator()(const slangd::semantic::SymbolKey& key) const -> size_t {
    size_t hash = std::hash<uint32_t>()(key.bufferId);
    hash ^= std::hash<size_t>()(key.offset) + 0x9e3779b9 + (hash << 6) +
            (hash >> 2);
    return hash;
  }
};
}  // namespace std

namespace slangd::semantic {

// SemanticIndex replaces separate DefinitionIndex and SymbolIndex with a single
// system that processes ALL symbol types for complete LSP coverage
class SemanticIndex {
 public:
  struct SymbolInfo {
    const slang::ast::Symbol* symbol{};
    slang::SourceLocation location;
    lsp::SymbolKind lsp_kind{};
    lsp::Range range{};
    const slang::ast::Scope* parent{};
    bool is_definition{false};
    slang::SourceRange definition_range;
    uint32_t buffer_id{};
  };

  static auto FromCompilation(
      slang::ast::Compilation& compilation,
      const slang::SourceManager& source_manager)
      -> std::unique_ptr<SemanticIndex>;

  // Query methods
  auto GetSymbolCount() const -> size_t {
    return symbols_.size();
  }

  auto GetSymbolAt(slang::SourceLocation location) const
      -> std::optional<SymbolInfo>;

  auto GetAllSymbols() const
      -> const std::unordered_map<slang::SourceLocation, SymbolInfo>&;

  // SymbolIndex-compatible API
  auto GetDocumentSymbols(const std::string& uri) const
      -> std::vector<lsp::DocumentSymbol>;

  // DefinitionIndex-compatible API
  auto GetDefinitionRanges() const
      -> const std::unordered_map<SymbolKey, slang::SourceRange>& {
    return definition_ranges_;
  }

  auto GetReferenceMap() const
      -> const std::unordered_map<slang::SourceRange, SymbolKey>& {
    return reference_map_;
  }

  auto GetDefinitionRange(const SymbolKey& key) const
      -> std::optional<slang::SourceRange>;

  auto LookupSymbolAt(slang::SourceLocation loc) const
      -> std::optional<SymbolKey>;

 private:
  explicit SemanticIndex() = default;

  // Core data storage
  std::unordered_map<slang::SourceLocation, SymbolInfo> symbols_;

  // Definition indexing data structures
  std::unordered_map<SymbolKey, slang::SourceRange> definition_ranges_;
  std::unordered_map<slang::SourceRange, SymbolKey> reference_map_;

  // Store source manager reference for symbol processing
  const slang::SourceManager* source_manager_ = nullptr;

  // Visitor for symbol collection and reference tracking
  class IndexVisitor : public slang::ast::ASTVisitor<IndexVisitor, true, true> {
   public:
    explicit IndexVisitor(
        SemanticIndex* index, const slang::SourceManager* source_manager)
        : index_(index), source_manager_(source_manager) {
    }

    // Universal pre-visit hook for symbols
    template <typename T>
    void preVisit(const T& symbol) {
      if constexpr (std::is_base_of_v<slang::ast::Symbol, T>) {
        ProcessSymbol(symbol);
      }
    }

    // Reference tracking for NamedValueExpression
    void handle(const slang::ast::NamedValueExpression& expr);

    // Reference tracking for VariableSymbol (type references)
    void handle(const slang::ast::VariableSymbol& symbol);

    // Default traversal
    template <typename T>
    void handle(const T& node) {
      this->visitDefault(node);
    }

   private:
    SemanticIndex* index_;
    const slang::SourceManager* source_manager_;

    void ProcessSymbol(const slang::ast::Symbol& symbol);
  };

  // Utility methods ported from existing indexes
  static auto UnwrapSymbol(const slang::ast::Symbol& symbol)
      -> const slang::ast::Symbol&;

  static auto ConvertToLspKind(const slang::ast::Symbol& symbol)
      -> lsp::SymbolKind;

  static auto ComputeLspRange(
      const slang::ast::Symbol& symbol,
      const slang::SourceManager& source_manager) -> lsp::Range;

  static auto ShouldIndex(const slang::ast::Symbol& symbol) -> bool;

  // Definition range extraction from syntax nodes
  static auto ExtractDefinitionRange(
      const slang::ast::Symbol& symbol, const slang::syntax::SyntaxNode& syntax)
      -> slang::SourceRange;

  // Helper methods for document symbol building
  auto BuildDocumentSymbolTree(const std::string& uri) const
      -> std::vector<lsp::DocumentSymbol>;
  static auto CreateDocumentSymbol(const SymbolInfo& info)
      -> lsp::DocumentSymbol;
  auto AttachChildrenToSymbol(
      lsp::DocumentSymbol& parent, const slang::ast::Scope* parent_scope,
      const std::unordered_map<
          const slang::ast::Scope*, std::vector<const SymbolInfo*>>&
          children_map) const -> void;
  auto HandleEnumTypeAlias(
      lsp::DocumentSymbol& enum_doc_symbol,
      const slang::ast::Symbol* type_alias_symbol) const -> void;
  auto HandleStructTypeAlias(
      lsp::DocumentSymbol& struct_doc_symbol,
      const slang::ast::Symbol* type_alias_symbol) const -> void;
};

}  // namespace slangd::semantic

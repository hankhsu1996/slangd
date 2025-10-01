#pragma once

#include <memory>
#include <optional>
#include <utility>
#include <vector>

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

// Unified storage for references with embedded definition information
// Combines reference location and definition range for efficient lookups
struct ReferenceEntry {
  slang::SourceRange source_range;   // Where reference appears in source
  slang::SourceLocation target_loc;  // Target symbol location (for dedup)
  slang::SourceRange target_range;   // Definition range (captured immediately!)
  lsp::SymbolKind symbol_kind;       // For rich LSP responses
  std::string symbol_name;           // For debugging/logging
};

// Unified semantic entry combining both definitions and references
// Replaces dual SymbolInfo/ReferenceEntry architecture with single model
struct SemanticEntry {
  // Identity & Location
  slang::SourceRange source_range;  // Where this entry appears
  slang::SourceLocation location;   // Unique key

  // Symbol Information
  const slang::ast::Symbol* symbol;  // The actual symbol
  lsp::SymbolKind lsp_kind;          // LSP classification
  std::string name;                  // Display name

  // Hierarchy (DocumentSymbol tree)
  const slang::ast::Scope* parent;  // Parent scope for tree building

  // Reference Tracking (Go-to-definition)
  bool is_definition;                   // true = self-ref, false = cross-ref
  slang::SourceRange definition_range;  // Target definition location

  // Metadata
  slang::BufferID buffer_id;  // For file filtering

  // Factory method to construct SemanticEntry from symbol
  static auto Make(
      const slang::ast::Symbol& symbol, std::string_view name,
      slang::SourceRange source_range, bool is_definition,
      slang::SourceRange definition_range,
      const slang::ast::Scope* parent_scope) -> SemanticEntry;
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
  static auto FromCompilation(
      slang::ast::Compilation& compilation,
      const slang::SourceManager& source_manager,
      const std::string& current_file_uri) -> std::unique_ptr<SemanticIndex>;

  // Query methods

  auto GetSourceManager() const -> const slang::SourceManager& {
    return source_manager_.get();
  }

  // SymbolIndex-compatible API
  auto GetDocumentSymbols(const std::string& uri) const
      -> std::vector<lsp::DocumentSymbol>;

  // Reference access for testing and debugging
  auto GetReferences() const -> const std::vector<ReferenceEntry>& {
    return references_;
  }

  // Unified semantic entries access
  auto GetSemanticEntries() const -> const std::vector<SemanticEntry>& {
    return semantic_entries_;
  }

  // Find definition range for the symbol at the given location
  auto LookupDefinitionAt(slang::SourceLocation loc) const
      -> std::optional<slang::SourceRange>;

  // Validation method to check for overlapping ranges
  void ValidateNoRangeOverlaps() const;

 private:
  explicit SemanticIndex(const slang::SourceManager& source_manager)
      : source_manager_(source_manager) {
  }

  // Core data storage
  // Unified reference+definition storage for go-to-definition functionality
  std::vector<ReferenceEntry> references_;

  // New unified storage combining definitions and references (Phase 1)
  // Will eventually replace symbols_ and references_
  std::vector<SemanticEntry> semantic_entries_;

  // Store source manager reference for symbol processing
  std::reference_wrapper<const slang::SourceManager> source_manager_;

  // Visitor for symbol collection and reference tracking
  class IndexVisitor : public slang::ast::ASTVisitor<IndexVisitor, true, true> {
   public:
    explicit IndexVisitor(
        SemanticIndex& index, const slang::SourceManager& source_manager,
        std::string current_file_uri)
        : index_(index),
          source_manager_(source_manager),
          current_file_uri_(std::move(current_file_uri)) {
    }

    // Expression handlers
    void handle(const slang::ast::NamedValueExpression& expr);
    void handle(const slang::ast::CallExpression& expr);
    void handle(const slang::ast::ConversionExpression& expr);
    void handle(const slang::ast::MemberAccessExpression& expr);
    void handle(const slang::ast::HierarchicalValueExpression& expr);

    // Symbol handlers
    void handle(const slang::ast::VariableSymbol& symbol);
    void handle(const slang::ast::WildcardImportSymbol& import_symbol);
    void handle(const slang::ast::ExplicitImportSymbol& import_symbol);
    void handle(const slang::ast::ParameterSymbol& param);
    void handle(const slang::ast::SubroutineSymbol& subroutine);
    void handle(const slang::ast::DefinitionSymbol& definition);
    void handle(const slang::ast::TypeAliasType& type_alias);
    void handle(const slang::ast::EnumValueSymbol& enum_value);
    void handle(const slang::ast::FieldSymbol& field);
    void handle(const slang::ast::NetSymbol& net);
    void handle(const slang::ast::InterfacePortSymbol& interface_port);
    void handle(const slang::ast::ModportSymbol& modport);
    void handle(const slang::ast::ModportPortSymbol& modport_port);
    void handle(const slang::ast::GenerateBlockArraySymbol& generate_array);
    void handle(const slang::ast::GenerateBlockSymbol& generate_block);
    void handle(const slang::ast::GenvarSymbol& genvar);
    void handle(const slang::ast::PackageSymbol& package);
    void handle(const slang::ast::StatementBlockSymbol& statement_block);

    template <typename T>
    void handle(const T& node) {
      this->visitDefault(node);
    }

   private:
    std::reference_wrapper<SemanticIndex> index_;
    std::reference_wrapper<const slang::SourceManager> source_manager_;
    std::string current_file_uri_;

    // Helper methods
    // Unified type traversal - handles all type structure recursively
    void TraverseType(const slang::ast::Type& type);

    // Helper to create reference entries (source -> target)
    void CreateReference(
        slang::SourceRange source_range, slang::SourceRange definition_range,
        const slang::ast::Symbol& target_symbol);
  };
};

}  // namespace slangd::semantic

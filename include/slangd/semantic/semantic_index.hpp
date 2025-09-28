#pragma once

#include <memory>
#include <optional>
#include <unordered_map>
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
      const slang::SourceManager& source_manager,
      const std::string& current_file_uri) -> std::unique_ptr<SemanticIndex>;

  // Query methods
  auto GetSymbolCount() const -> size_t {
    return symbols_.size();
  }

  auto GetSymbolAt(slang::SourceLocation location) const
      -> std::optional<SymbolInfo>;

  auto GetAllSymbols() const
      -> const std::unordered_map<slang::SourceLocation, SymbolInfo>&;

  auto GetSourceManager() const -> const slang::SourceManager* {
    return source_manager_;
  }

  // SymbolIndex-compatible API
  auto GetDocumentSymbols(const std::string& uri) const
      -> std::vector<lsp::DocumentSymbol>;

  // Reference access for testing and debugging
  auto GetReferences() const -> const std::vector<ReferenceEntry>& {
    return references_;
  }

  // Find definition range for the symbol at the given location
  auto LookupDefinitionAt(slang::SourceLocation loc) const
      -> std::optional<slang::SourceRange>;

 private:
  explicit SemanticIndex() = default;

  // Core data storage
  std::unordered_map<slang::SourceLocation, SymbolInfo> symbols_;

  // Unified reference+definition storage for go-to-definition functionality
  std::vector<ReferenceEntry> references_;

  // Store source manager reference for symbol processing
  const slang::SourceManager* source_manager_ = nullptr;

  // Visitor for symbol collection and reference tracking
  class IndexVisitor : public slang::ast::ASTVisitor<IndexVisitor, true, true> {
   public:
    explicit IndexVisitor(
        SemanticIndex* index, const slang::SourceManager* source_manager,
        std::string current_file_uri)
        : index_(index),
          source_manager_(source_manager),
          current_file_uri_(std::move(current_file_uri)) {
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

    // Reference tracking for CallExpression (function/task calls)
    void handle(const slang::ast::CallExpression& expr);

    // Reference tracking for ConversionExpression (type casts)
    void handle(const slang::ast::ConversionExpression& expr);

    // Reference tracking for MemberAccessExpression (struct/union field access)
    void handle(const slang::ast::MemberAccessExpression& expr);

    // Reference tracking for VariableSymbol (type references)
    void handle(const slang::ast::VariableSymbol& symbol);

    // Reference tracking for WildcardImportSymbol (package references)
    void handle(const slang::ast::WildcardImportSymbol& import_symbol);

    // Reference tracking for ExplicitImportSymbol (package references)
    void handle(const slang::ast::ExplicitImportSymbol& import_symbol);

    // Definition handlers for self-references
    void handle(const slang::ast::ParameterSymbol& param);
    void handle(const slang::ast::SubroutineSymbol& subroutine);
    void handle(const slang::ast::DefinitionSymbol& definition);
    void handle(const slang::ast::TypeAliasType& type_alias);
    void handle(const slang::ast::EnumValueSymbol& enum_value);
    void handle(const slang::ast::FieldSymbol& field);
    void handle(const slang::ast::NetSymbol& net);
    void handle(const slang::ast::PortSymbol& port);
    void handle(const slang::ast::InterfacePortSymbol& interface_port);
    void handle(const slang::ast::ModportSymbol& modport);
    void handle(const slang::ast::ModportPortSymbol& modport_port);

    // Default traversal
    template <typename T>
    void handle(const T& node) {
      this->visitDefault(node);
    }

   private:
    SemanticIndex* index_;
    const slang::SourceManager* source_manager_;
    std::string current_file_uri_;

    void ProcessSymbol(const slang::ast::Symbol& symbol);
    void TraverseCompoundTypeMembers(const slang::ast::Type& type);

    // Helper to process all dimension specifier types comprehensively
    void ProcessVariableDimensions(
        const slang::ast::VariableSymbol& symbol,
        const slang::syntax::SyntaxList<slang::syntax::VariableDimensionSyntax>&
            dimensions);

    // Generic dimension processor for any scope context
    void ProcessDimensionsInScope(
        const slang::ast::Scope& scope,
        const slang::syntax::SyntaxList<slang::syntax::VariableDimensionSyntax>&
            dimensions);

    // Helper to process integer type packed dimensions
    void ProcessIntegerTypeDimensions(
        const slang::ast::Scope& scope,
        const slang::syntax::DataTypeSyntax& type_syntax);

    // Helper to create reference entries (source -> target)
    void CreateReference(
        slang::SourceRange source_range,
        const slang::ast::Symbol& target_symbol);
  };
};

}  // namespace slangd::semantic

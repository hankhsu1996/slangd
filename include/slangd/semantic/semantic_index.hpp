#pragma once

#include <memory>
#include <optional>
#include <unordered_set>
#include <utility>
#include <vector>

#include <asio/cancellation_signal.hpp>
#include <lsp/document_features.hpp>
#include <slang/ast/ASTVisitor.h>
#include <slang/ast/Compilation.h>
#include <slang/ast/Symbol.h>
#include <slang/text/SourceLocation.h>
#include <slang/text/SourceManager.h>
#include <spdlog/spdlog.h>

#include "slangd/semantic/definition_extractor.hpp"
#include "slangd/utils/canonical_path.hpp"

namespace slangd::services {
class PreambleManager;
}

namespace slangd::semantic {

// Unified semantic entry combining both definitions and references
// Replaces dual SymbolInfo/ReferenceEntry architecture with single model
struct SemanticEntry {
  // NEW: LSP Coordinate Fields (Phase 1 - Parallel Data)
  lsp::Range source_range_lsp;      // Where this entry appears (LSP coords)
  lsp::Position location_lsp;       // Unique key (LSP coords)
  lsp::Range definition_range_lsp;  // Target definition location (LSP coords)
  std::string source_uri;           // File where this entry appears
  std::string definition_uri;       // File where definition is
  bool is_cross_file;               // true = from preamble, false = local

  // OLD: Slang Coordinate Fields (DEPRECATED - will remove after migration)
  slang::SourceRange source_range;  // Where this entry appears
  slang::SourceLocation location;   // Unique key

  // Symbol Information
  const slang::ast::Symbol* symbol;  // The actual symbol
  lsp::SymbolKind lsp_kind;          // LSP classification
  std::string name;                  // Display name

  // Hierarchy (DocumentSymbol tree)
  const slang::ast::Scope* parent;          // Parent scope for tree building
  const slang::ast::Scope* children_scope;  // Where to find children (for
                                            // non-Scope symbols like
                                            // GenericClassDef)

  // Reference Tracking (Go-to-definition)
  bool is_definition;                   // true = self-ref, false = cross-ref
  slang::SourceRange definition_range;  // Target definition location (OLD)

  // Cross-file definitions (OLD - replaced by definition_uri)
  std::optional<CanonicalPath> cross_file_path;
  std::optional<lsp::Range> cross_file_range;

  // Metadata (OLD - replaced by source_uri filtering)
  slang::BufferID buffer_id;  // For file filtering

  // Factory method to construct SemanticEntry from symbol
  static auto Make(
      const slang::ast::Symbol& symbol, std::string_view name,
      slang::SourceRange source_range, bool is_definition,
      slang::SourceRange definition_range,
      const slang::ast::Scope* parent_scope,
      const slang::ast::Scope* children_scope = nullptr) -> SemanticEntry;
};

// OLD: Result of definition lookup - can be either same-file or cross-file
// DEPRECATED - will remove after migration
struct DefinitionLocation {
  // For same-file definitions (buffer exists in current compilation)
  std::optional<slang::SourceRange> same_file_range;

  // For cross-file definitions (from PreambleManager, compilation-independent)
  std::optional<CanonicalPath> cross_file_path;
  std::optional<lsp::Range> cross_file_range;
};

}  // namespace slangd::semantic

namespace slangd::semantic {

// SemanticIndex replaces separate DefinitionIndex and SymbolIndex with a single
// system that processes ALL symbol types for complete LSP coverage
class SemanticIndex {
 public:
  static auto FromCompilation(
      slang::ast::Compilation& compilation,
      const slang::SourceManager& source_manager,
      const std::string& current_file_uri,
      const services::PreambleManager* preamble_manager = nullptr,
      std::shared_ptr<spdlog::logger> logger = nullptr)
      -> std::unique_ptr<SemanticIndex>;

  // Query methods

  // GetSourceManager() - Still needed for:
  // 1. DocumentSymbolBuilder filtering (will be removed in Phase 5)
  // 2. Internal indexing (has its own reference)
  // 3. Validation methods (ValidateSymbolCoverage)
  // Note: Phase 5 will eliminate DocumentSymbolBuilder dependency
  [[nodiscard]] auto GetSourceManager() const -> const slang::SourceManager& {
    return source_manager_.get();
  }

  // SymbolIndex-compatible API
  [[nodiscard]] auto GetDocumentSymbols(const std::string& uri) const
      -> std::vector<lsp::DocumentSymbol>;

  // Unified semantic entries access
  [[nodiscard]] auto GetSemanticEntries() const
      -> const std::vector<SemanticEntry>& {
    return semantic_entries_;
  }

  // NEW (Phase 3): Find definition using LSP coordinates
  // No SourceManager needed - works with LSP ranges directly
  [[nodiscard]] auto LookupDefinitionAt(
      const std::string& uri, lsp::Position position) const
      -> std::optional<lsp::Location>;

  // OLD (DEPRECATED): Find definition using Slang coordinates
  // Will be removed after all callers migrated to new overload
  [[nodiscard]] auto LookupDefinitionAt(slang::SourceLocation loc) const
      -> std::optional<DefinitionLocation>;

  // Validation method to check for overlapping ranges
  void ValidateNoRangeOverlaps() const;

  // Validation method to check symbol coverage
  // Logs identifiers that don't have definitions in the semantic index
  // Only checks identifiers from the specified file URI
  void ValidateSymbolCoverage(
      slang::ast::Compilation& compilation,
      const std::string& current_file_uri) const;

 private:
  explicit SemanticIndex(
      const slang::SourceManager& source_manager,
      std::shared_ptr<spdlog::logger> logger)
      : source_manager_(source_manager),
        logger_(logger ? logger : spdlog::default_logger()) {
  }

  // Helper to check if a symbol is defined in the current file
  // Returns false for preamble symbols (separate compilation)
  static auto IsInCurrentFile(
      const slang::ast::Symbol& symbol, const std::string& current_file_uri,
      const slang::SourceManager& source_manager,
      const services::PreambleManager* preamble_manager) -> bool;

  // Helper for syntax tree locations (use IsInCurrentFile for symbols)
  static auto IsInCurrentFile(
      slang::SourceLocation loc, const std::string& current_file_uri,
      const slang::SourceManager& source_manager) -> bool;

  // Core data storage
  // Unified storage combining definitions and references
  std::vector<SemanticEntry> semantic_entries_;

  // Store source manager reference for symbol processing
  std::reference_wrapper<const slang::SourceManager> source_manager_;

  // Logger for error reporting
  std::shared_ptr<spdlog::logger> logger_;

  // Visitor for symbol collection and reference tracking
  class IndexVisitor : public slang::ast::ASTVisitor<IndexVisitor, true, true> {
   public:
    explicit IndexVisitor(
        SemanticIndex& index, const slang::SourceManager& source_manager,
        std::string current_file_uri,
        const services::PreambleManager* preamble_manager)
        : index_(index),
          source_manager_(source_manager),
          current_file_uri_(std::move(current_file_uri)),
          preamble_manager_(preamble_manager),
          definition_extractor_(index.logger_) {
    }

    // Expression handlers
    void handle(const slang::ast::NamedValueExpression& expr);
    void handle(const slang::ast::CallExpression& expr);
    void handle(const slang::ast::ConversionExpression& expr);
    void handle(const slang::ast::DataTypeExpression& expr);
    void handle(const slang::ast::MemberAccessExpression& expr);
    void handle(const slang::ast::HierarchicalValueExpression& expr);
    void handle(const slang::ast::StructuredAssignmentPatternExpression& expr);

    // Symbol handlers
    void handle(const slang::ast::FormalArgumentSymbol& formal_arg);
    void handle(const slang::ast::VariableSymbol& symbol);
    void handle(const slang::ast::WildcardImportSymbol& import_symbol);
    void handle(const slang::ast::ExplicitImportSymbol& import_symbol);
    void handle(const slang::ast::ParameterSymbol& param);
    void handle(const slang::ast::SubroutineSymbol& subroutine);
    void handle(const slang::ast::MethodPrototypeSymbol& method_prototype);
    void handle(const slang::ast::DefinitionSymbol& definition);
    void handle(const slang::ast::TypeAliasType& type_alias);
    void handle(const slang::ast::EnumValueSymbol& enum_value);
    void handle(const slang::ast::FieldSymbol& field);
    void handle(const slang::ast::NetSymbol& net);
    void handle(const slang::ast::ClassPropertySymbol& class_property);
    void handle(const slang::ast::GenericClassDefSymbol& class_def);
    void handle(const slang::ast::ClassType& class_type);
    void handle(const slang::ast::InterfacePortSymbol& interface_port);
    void handle(const slang::ast::ModportSymbol& modport);
    void handle(const slang::ast::ModportPortSymbol& modport_port);
    void handle(const slang::ast::InstanceArraySymbol& instance_array);
    void handle(const slang::ast::InstanceSymbol& instance);
    void handle(const slang::ast::GenerateBlockArraySymbol& generate_array);
    void handle(const slang::ast::GenerateBlockSymbol& generate_block);
    void handle(const slang::ast::GenvarSymbol& genvar);
    void handle(const slang::ast::PackageSymbol& package);
    void handle(const slang::ast::StatementBlockSymbol& statement_block);
    void handle(const slang::ast::UninstantiatedDefSymbol& symbol);

    template <typename T>
    void handle(const T& node) {
      this->visitDefault(node);
    }

   private:
    std::reference_wrapper<SemanticIndex> index_;
    std::reference_wrapper<const slang::SourceManager> source_manager_;
    std::string current_file_uri_;
    const services::PreambleManager* preamble_manager_;

    // Definition extractor for precise symbol range extraction
    DefinitionExtractor definition_extractor_;

    // Track which type syntax nodes we've already traversed
    // Prevents duplicate traversal when multiple symbols share the same type
    // syntax (e.g., `logic [WIDTH-1:0] var_a, var_b;` - both variables share
    // same type)
    std::unordered_set<const slang::syntax::SyntaxNode*> visited_type_syntaxes_;

    // Track visited generate condition expressions to avoid duplicates
    // Multiple generate blocks (if/else branches, case branches) share the same
    // condition expression pointer
    std::unordered_set<const slang::ast::Expression*>
        visited_generate_conditions_;

    // Helper methods for adding semantic entries
    void AddEntry(SemanticEntry entry);

    void AddDefinition(
        const slang::ast::Symbol& symbol, std::string_view name,
        slang::SourceRange range, const slang::ast::Scope* parent_scope,
        const slang::ast::Scope* children_scope = nullptr);

    void AddReference(
        const slang::ast::Symbol& symbol, std::string_view name,
        slang::SourceRange source_range, slang::SourceRange definition_range,
        const slang::ast::Scope* parent_scope);

    void AddCrossFileReference(
        const slang::ast::Symbol& symbol, std::string_view name,
        slang::SourceRange source_range, slang::SourceRange definition_range,
        const slang::SourceManager& preamble_manager_source_manager,
        const slang::ast::Scope* parent_scope);

    // Unified type traversal - handles all type structure recursively
    void TraverseType(const slang::ast::Type& type);

    // Helper for indexing class specialization (e.g., Class#(.PARAM(value)))
    // Traverses call syntax to find ClassNameSyntax nodes and link to
    // genericClass
    void IndexClassSpecialization(
        const slang::ast::ClassType& class_type,
        const slang::syntax::SyntaxNode* call_syntax);

    // Recursively traverse scoped names to find and index ClassName nodes
    void TraverseClassNames(
        const slang::syntax::SyntaxNode* node,
        const slang::ast::ClassType& class_type,
        slang::SourceRange definition_range);

    // Helper for indexing class parameter names in specialization
    void IndexClassParameters(
        const slang::ast::ClassType& class_type,
        const slang::syntax::ParameterValueAssignmentSyntax& params);

    // Helper for indexing interface/module parameter names in instantiation
    void IndexInstanceParameters(
        const slang::ast::InstanceSymbol& instance,
        const slang::syntax::ParameterValueAssignmentSyntax& params);

    // Helper for indexing package names in scoped references
    // (e.g., pkg::PARAM, pkg::func())
    void IndexPackageInScopedName(
        const slang::syntax::SyntaxNode* syntax,
        const slang::ast::Symbol& target_symbol);
  };
};

}  // namespace slangd::semantic

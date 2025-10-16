#pragma once

#include <expected>
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

#include "slangd/utils/conversion.hpp"

namespace slangd::services {
class PreambleManager;
}

namespace slangd::semantic {

// Unified semantic entry combining both definitions and references
// Replaces dual SymbolInfo/ReferenceEntry architecture with single model
// Stores LSP coordinates for compilation-independent processing
//
// INVARIANT: All entries in a SemanticIndex have source locations in the same
// file (the file being indexed). Symbols from included files are filtered out.
struct SemanticEntry {
  // LSP coordinates (reference location always in current_file_uri)
  lsp::Range ref_range;
  lsp::Location def_loc;  // Definition location (range + URI)
  bool is_cross_file;     // true = from preamble, false = local

  // Symbol information
  const slang::ast::Symbol* symbol;
  lsp::SymbolKind lsp_kind;
  std::string name;

  // Hierarchy for DocumentSymbol tree
  const slang::ast::Scope* parent;
  const slang::ast::Scope* children_scope;  // For non-Scope symbols like
                                            // GenericClassDef

  // Reference tracking
  bool is_definition;
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
      -> std::expected<std::unique_ptr<SemanticIndex>, std::string>;

  // Query methods

  // GetSourceManager() - Still needed for:
  // 1. DocumentSymbolBuilder enum/struct members (not in SemanticIndex)
  // 2. ValidateSymbolCoverage (needs to check symbol.location.valid())
  // 3. Internal indexing (has its own reference via source_manager_)
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

  // Find definition using LSP coordinates (no SourceManager needed)
  [[nodiscard]] auto LookupDefinitionAt(
      const std::string& uri, lsp::Position position) const
      -> std::optional<lsp::Location>;

  void ValidateNoRangeOverlaps() const;

  // Check for invalid coordinates (line == -1) which indicate conversion
  // failures Returns error if any invalid coordinates are found (fail-fast
  // behavior)
  auto ValidateCoordinates() const -> std::expected<void, std::string>;

  // Logs identifiers that don't have definitions in the semantic index
  void ValidateSymbolCoverage(
      slang::ast::Compilation& compilation,
      const std::string& current_file_uri) const;

 private:
  explicit SemanticIndex(
      const slang::SourceManager& source_manager, std::string current_file_uri,
      std::shared_ptr<spdlog::logger> logger)
      : source_manager_(source_manager),
        current_file_uri_(std::move(current_file_uri)),
        logger_(logger ? logger : spdlog::default_logger()) {
  }

  // Returns false for preamble symbols (separate compilation)
  static auto IsInCurrentFile(
      const slang::ast::Symbol& symbol, const std::string& current_file_uri,
      const slang::SourceManager& source_manager,
      const services::PreambleManager* preamble_manager) -> bool;

  static auto IsInCurrentFile(
      slang::SourceLocation loc, const std::string& current_file_uri,
      const slang::SourceManager& source_manager) -> bool;

  // Unified storage for definitions and references
  std::vector<SemanticEntry> semantic_entries_;

  std::reference_wrapper<const slang::SourceManager> source_manager_;

  // All entries must have source locations in this file
  std::string current_file_uri_;

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
          preamble_manager_(preamble_manager) {
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

    // Get accumulated indexing errors (e.g., BufferID mismatches)
    [[nodiscard]] auto GetIndexingErrors() const
        -> const std::vector<std::string>& {
      return indexing_errors_;
    }

   private:
    std::reference_wrapper<SemanticIndex> index_;
    std::reference_wrapper<const slang::SourceManager> source_manager_;
    std::string current_file_uri_;
    const services::PreambleManager* preamble_manager_;

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

    // Track indexing errors (e.g., BufferID mismatches indicating missing
    // preamble symbols)
    std::vector<std::string> indexing_errors_;

    // Helper methods for adding semantic entries
    void AddEntry(SemanticEntry entry);

    void AddDefinition(
        const slang::ast::Symbol& symbol, std::string_view name,
        slang::SourceRange range, const slang::ast::Scope* parent_scope,
        const slang::ast::Scope* children_scope = nullptr);

    void AddReference(
        const slang::ast::Symbol& symbol, std::string_view name,
        lsp::Range ref_range, lsp::Location def_loc,
        const slang::ast::Scope* parent_scope);

    // For references where PreambleManager provides pre-converted LSP
    // definition coordinates
    void AddReferenceWithLspDefinition(
        const slang::ast::Symbol& symbol, std::string_view name,
        lsp::Range ref_range, lsp::Location def_loc,
        const slang::ast::Scope* parent_scope);

    // Helper to convert Slang reference ranges to LSP coordinates
    [[nodiscard]] auto ConvertRefRange(slang::SourceRange range) const
        -> lsp::Range {
      return ToLspRange(range, source_manager_.get());
    }

    void TraverseType(const slang::ast::Type& type);

    void IndexClassSpecialization(
        const slang::ast::ClassType& class_type,
        const slang::syntax::SyntaxNode* call_syntax);

    void TraverseClassNames(
        const slang::syntax::SyntaxNode* node,
        const slang::ast::ClassType& class_type,
        slang::SourceRange definition_range);

    void IndexClassParameters(
        const slang::ast::ClassType& class_type,
        const slang::syntax::ParameterValueAssignmentSyntax& params);

    void IndexInstanceParameters(
        const slang::ast::InstanceSymbol& instance,
        const slang::syntax::ParameterValueAssignmentSyntax& params);

    void IndexPackageInScopedName(
        const slang::syntax::SyntaxNode* syntax,
        const slang::ast::Symbol& target_symbol);
  };
};

}  // namespace slangd::semantic

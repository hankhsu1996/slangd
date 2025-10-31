#pragma once

#include <expected>
#include <memory>
#include <optional>
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

namespace slangd::services {
class PreambleManager;
}

namespace slangd::semantic {

// Forward declaration
class IndexVisitor;

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
      const std::string& current_file_uri, slang::BufferID current_file_buffer,
      const services::PreambleManager* preamble_manager = nullptr,
      std::shared_ptr<spdlog::logger> logger = spdlog::default_logger())
      -> std::expected<std::unique_ptr<SemanticIndex>, std::string>;

  // Query methods

  // GetSourceManager() - Still needed for:
  // 1. ValidateSymbolCoverage (needs to check symbol.location.valid())
  // 2. Internal indexing (has its own reference via source_manager_)
  [[nodiscard]] auto GetSourceManager() const -> const slang::SourceManager& {
    return source_manager_.get();
  }

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

  // IndexVisitor needs access to semantic_entries_ and logger_
  friend class IndexVisitor;
};

}  // namespace slangd::semantic

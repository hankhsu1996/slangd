#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include <lsp/document_features.hpp>
#include <slang/ast/Compilation.h>
#include <slang/text/SourceManager.h>
#include <spdlog/spdlog.h>

namespace slangd::semantic {

class SymbolIndex {
 public:
  // Factory method to create SymbolIndex from compilation
  static auto FromCompilation(
      slang::ast::Compilation& compilation,
      const slang::SourceManager& source_manager,
      std::shared_ptr<spdlog::logger> logger = nullptr)
      -> std::unique_ptr<SymbolIndex>;

  // Main LSP method: Get document symbols for given URI
  [[nodiscard]] auto GetDocumentSymbols(const std::string& uri) const
      -> std::vector<lsp::DocumentSymbol>;

 private:
  explicit SymbolIndex(
      slang::ast::Compilation& compilation,
      const slang::SourceManager& source_manager,
      std::shared_ptr<spdlog::logger> logger);

  // Core data members
  std::reference_wrapper<slang::ast::Compilation> compilation_;
  std::reference_wrapper<const slang::SourceManager> source_manager_;
  std::shared_ptr<spdlog::logger> logger_;

  // Core resolver
  [[nodiscard]] auto ResolveSymbolsFromCompilation(const std::string& uri) const
      -> std::vector<lsp::DocumentSymbol>;

  // Symbol hierarchy builder
  void BuildSymbolHierarchy(
      const slang::ast::Symbol& symbol,
      std::vector<lsp::DocumentSymbol>& document_symbols,
      const std::string& uri,
      std::unordered_set<std::string>& seen_names) const;

  void BuildSymbolChildren(
      const slang::ast::Symbol& symbol, lsp::DocumentSymbol& parent_symbol,
      const std::string& uri) const;

  void BuildScopeSymbolChildren(
      const slang::ast::Scope& scope, lsp::DocumentSymbol& parent_symbol,
      const std::string& uri) const;

  // Conversion utilities
  static auto ConvertSymbolKindToLsp(const slang::ast::Symbol& symbol)
      -> lsp::SymbolKind;

  static auto ConvertSymbolNameRangeToLsp(
      const slang::ast::Symbol& symbol,
      const slang::SourceManager& source_manager) -> lsp::Range;

  // Filter and unwrap
  static auto IsSymbolInDocument(
      const slang::ast::Symbol& symbol,
      const slang::SourceManager& source_manager, const std::string& uri)
      -> bool;

  static auto IsRelevantDocumentSymbol(const slang::ast::Symbol& symbol)
      -> bool;

  static auto IsSymbolInUriDocument(
      const slang::ast::Symbol& symbol,
      const slang::SourceManager& source_manager, const std::string& uri)
      -> bool;

  static auto GetUnwrappedSymbol(const slang::ast::Symbol& symbol)
      -> const slang::ast::Symbol&;
};

}  // namespace slangd::semantic

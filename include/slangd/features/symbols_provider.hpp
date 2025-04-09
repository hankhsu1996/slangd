#pragma once

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include <asio.hpp>
#include <lsp/document_features.hpp>
#include <slang/ast/Compilation.h>
#include <slang/ast/Symbol.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/text/SourceManager.h>
#include <spdlog/spdlog.h>

#include "slangd/core/document_manager.hpp"
#include "slangd/core/workspace_manager.hpp"
#include "slangd/features/language_feature_provider.hpp"

namespace slangd {

class SymbolsProvider : public LanguageFeatureProvider {
 public:
  SymbolsProvider(
      std::shared_ptr<DocumentManager> document_manager,
      std::shared_ptr<WorkspaceManager> workspace_manager,
      std::shared_ptr<spdlog::logger> logger = nullptr)
      : LanguageFeatureProvider(document_manager, workspace_manager, logger) {
  }

  // Public API
  auto GetSymbolsForUri(const std::string& uri)
      -> std::vector<lsp::DocumentSymbol>;

  // Core resolver
  auto ResolveSymbolsFromCompilation(
      slang::ast::Compilation& compilation,
      const std::shared_ptr<slang::SourceManager>& source_manager,
      const std::string& uri) -> std::vector<lsp::DocumentSymbol>;

 private:
  // Symbol hierarchy builder
  static void BuildSymbolHierarchy(
      const slang::ast::Symbol& symbol,
      std::vector<lsp::DocumentSymbol>& document_symbols,
      const std::shared_ptr<slang::SourceManager>& source_manager,
      const std::string& uri, std::unordered_set<std::string>& seen_names,
      slang::ast::Compilation& compilation);

  static void BuildSymbolChildren(
      const slang::ast::Symbol& symbol, lsp::DocumentSymbol& parent_symbol,
      const std::shared_ptr<slang::SourceManager>& source_manager,
      const std::string& uri, slang::ast::Compilation& compilation);

  static void BuildScopeSymbolChildren(
      const slang::ast::Scope& scope, lsp::DocumentSymbol& parent_symbol,
      const std::shared_ptr<slang::SourceManager>& source_manager,
      const std::string& uri, slang::ast::Compilation& compilation);

  // Conversion utilities
  static auto ConvertSymbolKindToLsp(const slang::ast::Symbol& symbol)
      -> lsp::SymbolKind;

  static auto ConvertSymbolNameRangeToLsp(
      const slang::ast::Symbol& symbol,
      const std::shared_ptr<slang::SourceManager>& source_manager)
      -> lsp::Range;

  // Filter and unwrap
  static auto IsSymbolInUriDocument(
      const slang::ast::Symbol& symbol,
      const std::shared_ptr<slang::SourceManager>& source_manager,
      const std::string& uri) -> bool;

  static auto GetUnwrappedSymbol(const slang::ast::Symbol& symbol)
      -> const slang::ast::Symbol&;
};

}  // namespace slangd

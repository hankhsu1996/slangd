#pragma once

#include <memory>

#include "slangd/core/document_manager.hpp"
#include "slangd/core/workspace_manager.hpp"
#include "slangd/features/language_feature_provider.hpp"

namespace slangd {

class DefinitionProvider : public LanguageFeatureProvider {
 public:
  DefinitionProvider(
      std::shared_ptr<DocumentManager> document_manager,
      std::shared_ptr<WorkspaceManager> workspace_manager,
      std::shared_ptr<spdlog::logger> logger = nullptr)
      : LanguageFeatureProvider(document_manager, workspace_manager, logger) {
  }

  // Public API
  auto GetDefinitionForUri(std::string uri, lsp::Position position)
      -> asio::awaitable<std::vector<lsp::Location>>;

  // Core resolver
  auto ResolveDefinitionFromCompilation(
      slang::ast::Compilation& compilation,
      const std::shared_ptr<slang::SourceManager>& source_manager,
      const slang::ast::Symbol& symbol)
      -> asio::awaitable<std::vector<lsp::Location>>;
};

}  // namespace slangd

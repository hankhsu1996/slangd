#pragma once

#include <memory>
#include <string>
#include <vector>

#include <slang/text/SourceLocation.h>
#include <spdlog/spdlog.h>

#include "lsp/basic.hpp"
#include "slangd/services/legacy/document_manager.hpp"
#include "slangd/services/legacy/language_feature_provider.hpp"
#include "slangd/services/legacy/workspace_manager.hpp"

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
      -> std::vector<lsp::Location>;

  auto GetDefinitionFromWorkspace(std::string uri, lsp::Position position)
      -> std::vector<lsp::Location>;

  // Core resolvers
  static auto ResolveDefinitionFromSymbolIndex(
      const semantic::SymbolIndex& index,
      const std::shared_ptr<slang::SourceManager>& source_manager,
      slang::SourceLocation location) -> std::vector<lsp::Location>;
};

}  // namespace slangd

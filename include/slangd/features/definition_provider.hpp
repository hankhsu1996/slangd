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
      : LanguageFeatureProvider(document_manager, workspace_manager),
        logger_(logger ? logger : spdlog::default_logger()) {
  }

  auto Logger() -> std::shared_ptr<spdlog::logger> {
    return logger_;
  }

  auto GetDefinitionAtPosition(std::string uri, lsp::Position position)
      -> asio::awaitable<std::vector<lsp::Location>>;

 private:
  std::shared_ptr<spdlog::logger> logger_;
};

}  // namespace slangd

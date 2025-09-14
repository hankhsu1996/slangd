#pragma once

#include <memory>
#include <utility>

#include "slangd/services/legacy/document_manager.hpp"
#include "slangd/services/legacy/workspace_manager.hpp"

namespace slangd {

// NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes)
class LanguageFeatureProvider {
 public:
  LanguageFeatureProvider(
      std::shared_ptr<DocumentManager> document_manager,
      std::shared_ptr<WorkspaceManager> workspace_manager,
      std::shared_ptr<spdlog::logger> logger = nullptr)
      : document_manager(std::move(document_manager)),
        workspace_manager(std::move(workspace_manager)),
        logger(logger ? logger : spdlog::default_logger()) {
  }

 protected:
  std::shared_ptr<DocumentManager> document_manager;
  std::shared_ptr<WorkspaceManager> workspace_manager;
  std::shared_ptr<spdlog::logger> logger;
};
// NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)

}  // namespace slangd

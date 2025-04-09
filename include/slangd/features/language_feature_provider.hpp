#pragma once

#include <memory>

#include "slangd/core/document_manager.hpp"
#include "slangd/core/workspace_manager.hpp"

namespace slangd {

class LanguageFeatureProvider {
 public:
  LanguageFeatureProvider(
      std::shared_ptr<DocumentManager> document_manager,
      std::shared_ptr<WorkspaceManager> workspace_manager,
      std::shared_ptr<spdlog::logger> logger = nullptr)
      : document_manager_(document_manager),
        workspace_manager_(workspace_manager),
        logger_(logger ? logger : spdlog::default_logger()) {
  }

 protected:
  std::shared_ptr<DocumentManager> document_manager_;
  std::shared_ptr<WorkspaceManager> workspace_manager_;
  std::shared_ptr<spdlog::logger> logger_;
};

}  // namespace slangd

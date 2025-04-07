#pragma once

#include <memory>

#include "slangd/core/document_manager.hpp"
#include "slangd/core/workspace_manager.hpp"

namespace slangd {

class LanguageFeatureProvider {
 public:
  LanguageFeatureProvider(
      std::shared_ptr<DocumentManager> document_manager,
      std::shared_ptr<WorkspaceManager> workspace_manager)
      : document_manager_(document_manager),
        workspace_manager_(workspace_manager) {
  }

 protected:
  std::shared_ptr<DocumentManager> document_manager_;
  std::shared_ptr<WorkspaceManager> workspace_manager_;
};

}  // namespace slangd

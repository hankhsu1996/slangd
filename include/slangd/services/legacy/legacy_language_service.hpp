#pragma once

#include <memory>

#include <asio.hpp>
#include <spdlog/spdlog.h>

#include "slangd/core/language_service_base.hpp"
#include "slangd/core/project_layout_service.hpp"
#include "slangd/services/legacy/document_manager.hpp"
#include "slangd/services/legacy/workspace_manager.hpp"

namespace slangd {

// Legacy service implementation that wraps current
// DocumentManager/WorkspaceManager Maintains exact same behavior as individual
// providers Provides foundation for future GlobalIndex and Hybrid services
class LegacyLanguageService : public LanguageServiceBase {
 public:
  // Constructor for late initialization (workspace set up later)
  explicit LegacyLanguageService(
      asio::any_io_executor executor,
      std::shared_ptr<spdlog::logger> logger = nullptr);

  // Initialize with workspace folder (called during LSP initialize)
  auto InitializeWorkspace(std::string workspace_uri)
      -> asio::awaitable<void> override;

  // LanguageServiceBase implementation
  auto ComputeDiagnostics(std::string uri, std::string content, int version)
      -> asio::awaitable<std::vector<lsp::Diagnostic>> override;

  auto GetDefinitionsForPosition(
      std::string uri, lsp::Position position, std::string content, int version)
      -> std::vector<lsp::Location> override;

  auto GetDocumentSymbols(std::string uri, std::string content, int version)
      -> std::vector<lsp::DocumentSymbol> override;

  auto HandleConfigChange() -> void override;

  auto HandleSourceFileChange() -> void override;

 private:
  // Core managers - same as current architecture
  std::shared_ptr<ProjectLayoutService> layout_service_;
  std::shared_ptr<DocumentManager> document_manager_;
  std::shared_ptr<WorkspaceManager> workspace_manager_;
  std::shared_ptr<spdlog::logger> logger_;
  asio::any_io_executor executor_;
};

}  // namespace slangd

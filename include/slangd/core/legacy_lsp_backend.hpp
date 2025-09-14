#pragma once

#include <memory>

#include <asio.hpp>
#include <spdlog/spdlog.h>

#include "slangd/core/config_manager.hpp"
#include "slangd/core/document_manager.hpp"
#include "slangd/core/lsp_backend_facade.hpp"
#include "slangd/core/workspace_manager.hpp"

namespace slangd {

// Legacy backend implementation that wraps current
// DocumentManager/WorkspaceManager Maintains exact same behavior as individual
// providers Provides foundation for future GlobalIndex and Hybrid backends
class LegacyLspBackend : public LspBackendFacade {
 public:
  // Constructor for late initialization (workspace set up later)
  explicit LegacyLspBackend(
      asio::any_io_executor executor,
      std::shared_ptr<spdlog::logger> logger = nullptr);

  // Initialize with workspace folder (called during LSP initialize)
  auto InitializeWorkspace(std::string workspace_uri) -> asio::awaitable<void>;

  // LspBackendFacade implementation
  auto ComputeDiagnostics(std::string uri, std::string content)
      -> asio::awaitable<std::vector<lsp::Diagnostic>> override;

  auto GetDefinitionsForPosition(std::string uri, lsp::Position position)
      -> std::vector<lsp::Location> override;

  auto GetDocumentSymbols(std::string uri)
      -> std::vector<lsp::DocumentSymbol> override;

 private:
  // Core managers - same as current architecture
  std::shared_ptr<ConfigManager> config_manager_;
  std::shared_ptr<DocumentManager> document_manager_;
  std::shared_ptr<WorkspaceManager> workspace_manager_;
  std::shared_ptr<spdlog::logger> logger_;
  asio::any_io_executor executor_;
};

}  // namespace slangd

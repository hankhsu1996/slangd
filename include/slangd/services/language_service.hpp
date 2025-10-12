#pragma once

#include <memory>
#include <string>
#include <vector>

#include <asio.hpp>
#include <asio/experimental/channel.hpp>
#include <spdlog/spdlog.h>

#include "slangd/core/language_service_base.hpp"
#include "slangd/core/project_layout_service.hpp"
#include "slangd/services/document_state_manager.hpp"
#include "slangd/services/global_catalog.hpp"
#include "slangd/services/open_document_tracker.hpp"
#include "slangd/services/overlay_session.hpp"
#include "slangd/services/session_manager.hpp"

namespace slangd::services {

// Service implementation using SessionManager for lifecycle management
// Creates fresh Compilation + SemanticIndex per LSP request
// Supports GlobalCatalog integration for cross-file functionality
class LanguageService : public LanguageServiceBase {
 public:
  // Constructor for late initialization (workspace set up later)
  explicit LanguageService(
      asio::any_io_executor executor,
      std::shared_ptr<spdlog::logger> logger = nullptr);

  // LanguageServiceBase implementation
  auto InitializeWorkspace(std::string workspace_uri)
      -> asio::awaitable<void> override;

  auto ComputeParseDiagnostics(std::string uri, std::string content)
      -> asio::awaitable<std::expected<
          std::vector<lsp::Diagnostic>, lsp::error::LspError>> override;

  auto ComputeDiagnostics(std::string uri) -> asio::awaitable<std::expected<
      std::vector<lsp::Diagnostic>, lsp::error::LspError>> override;

  auto GetDefinitionsForPosition(std::string uri, lsp::Position position)
      -> asio::awaitable<std::expected<
          std::vector<lsp::Location>, lsp::error::LspError>> override;

  auto GetDocumentSymbols(std::string uri) -> asio::awaitable<std::expected<
      std::vector<lsp::DocumentSymbol>, lsp::error::LspError>> override;

  auto HandleConfigChange() -> asio::awaitable<void> override;

  auto HandleSourceFileChange(std::string uri, lsp::FileChangeType change_type)
      -> asio::awaitable<void> override;

  // Document lifecycle events
  auto OnDocumentOpened(std::string uri, std::string content, int version)
      -> asio::awaitable<void> override;

  auto OnDocumentChanged(std::string uri, std::string content, int version)
      -> asio::awaitable<void> override;

  auto OnDocumentSaved(std::string uri) -> asio::awaitable<void> override;

  auto OnDocumentClosed(std::string uri) -> void override;

  auto OnDocumentsChanged(std::vector<std::string> uris) -> void override;

  auto GetDocumentState(std::string uri)
      -> asio::awaitable<std::optional<DocumentState>> override;

  auto GetAllOpenDocumentUris()
      -> asio::awaitable<std::vector<std::string>> override;

 private:
  // Session recovery helper: Gets session or rebuilds if evicted
  auto GetOrRebuildSession(std::string uri)
      -> asio::awaitable<std::shared_ptr<const OverlaySession>>;

  // Core dependencies
  std::shared_ptr<ProjectLayoutService> layout_service_;
  std::shared_ptr<const GlobalCatalog> global_catalog_;
  std::shared_ptr<spdlog::logger> logger_;
  asio::any_io_executor executor_;

  // Open document tracking (shared by doc_state_ and session_manager_)
  std::shared_ptr<OpenDocumentTracker> open_tracker_;

  // Document state management
  DocumentStateManager doc_state_;

  std::unique_ptr<SessionManager> session_manager_;

  // Background thread pool for parse diagnostics
  std::unique_ptr<asio::thread_pool> compilation_pool_;
  static constexpr size_t kThreadPoolSize = 4;
};

}  // namespace slangd::services

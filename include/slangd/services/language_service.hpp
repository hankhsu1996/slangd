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
#include "slangd/services/open_document_tracker.hpp"
#include "slangd/services/overlay_session.hpp"
#include "slangd/services/preamble_manager.hpp"
#include "slangd/services/session_manager.hpp"
#include "slangd/utils/broadcast_event.hpp"

namespace slangd::services {

// Service implementation using SessionManager for lifecycle management
// Creates fresh Compilation + SemanticIndex per LSP request
// Supports PreambleManager integration for cross-file functionality
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

  auto IsDocumentOpen(const std::string& uri) const -> bool override;

  // Set callback for publishing diagnostics to LSP client
  auto SetDiagnosticPublisher(DiagnosticPublisher publisher) -> void override {
    diagnostic_publisher_ = std::move(publisher);
  }

  // Set callback for publishing status updates to LSP client
  auto SetStatusPublisher(StatusPublisher publisher) -> void override {
    status_publisher_ = std::move(publisher);
  }

 private:
  // Helper to create diagnostic extraction hook for session creation
  auto CreateDiagnosticHook(std::string uri, int version)
      -> std::function<void(const CompilationState&)>;

  // Preamble rebuild helpers
  auto RebuildPreambleAndSessions() -> asio::awaitable<void>;
  auto ScheduleDebouncedPreambleRebuild() -> void;

  // Session rebuild helpers
  auto RebuildSessionWithDiagnostics(std::string uri) -> asio::awaitable<void>;
  auto ScheduleDebouncedSessionRebuild(std::string uri) -> void;

  // Core dependencies
  std::shared_ptr<ProjectLayoutService> layout_service_;
  std::shared_ptr<const PreambleManager> preamble_manager_;
  std::shared_ptr<spdlog::logger> logger_;
  asio::any_io_executor executor_;
  CanonicalPath workspace_root_;

  // Open document tracking (shared by doc_state_ and session_manager_)
  std::shared_ptr<OpenDocumentTracker> open_tracker_;

  // Document state management
  DocumentStateManager doc_state_;

  std::unique_ptr<SessionManager> session_manager_;

  // Workspace initialization synchronization events
  // config_ready: Config loaded, layout_service ready (syntax features can use
  // defines)
  utils::BroadcastEvent config_ready_;
  // workspace_ready: Preamble built, session_manager ready (semantic features
  // available)
  utils::BroadcastEvent workspace_ready_;

  // Background thread pool for parse diagnostics
  std::unique_ptr<asio::thread_pool> compilation_pool_;

  // Thread pool size: use half of hardware threads with minimum of 1
  static auto GetThreadPoolSize() -> size_t {
    auto hw_threads = std::thread::hardware_concurrency();
    return std::max(size_t{1}, static_cast<size_t>(hw_threads) / 2);
  }

  // Callback for publishing diagnostics (set by LSP server layer)
  DiagnosticPublisher diagnostic_publisher_;

  // Callback for publishing status updates (set by LSP server layer)
  StatusPublisher status_publisher_;

  // Preamble rebuild debouncing and concurrency protection
  std::optional<asio::steady_timer> preamble_rebuild_timer_;
  static constexpr auto kPreambleDebounceDelay = std::chrono::milliseconds(500);
  bool preamble_rebuild_in_progress_ = false;
  bool preamble_rebuild_pending_ = false;

  // Session rebuild debouncing and concurrency protection (per-URI)
  enum class RebuildState { kIdle, kInProgress, kPendingNext };
  std::map<std::string, asio::steady_timer> session_rebuild_timers_;
  std::map<std::string, RebuildState> session_rebuild_state_;
  static constexpr auto kSessionDebounceDelay = std::chrono::milliseconds(500);
};

}  // namespace slangd::services

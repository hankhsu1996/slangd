#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/thread_pool.hpp>
#include <slang/ast/Compilation.h>
#include <slang/text/SourceManager.h>
#include <spdlog/spdlog.h>

#include "slangd/core/project_layout_service.hpp"
#include "slangd/services/global_catalog.hpp"
#include "slangd/services/open_document_tracker.hpp"
#include "slangd/services/overlay_session.hpp"
#include "slangd/utils/broadcast_event.hpp"

namespace slangd::services {

// Intermediate state after Phase 1 (elaboration) - used for fast diagnostics
struct CompilationState {
  std::shared_ptr<slang::SourceManager> source_manager;
  std::shared_ptr<slang::ast::Compilation> compilation;
  slang::BufferID main_buffer_id;
};

// Session creation phase tracking
enum class SessionPhase {
  kElaborationComplete,  // Phase 1: Diagnostics can run
  kIndexingComplete      // Phase 2: Symbols/definitions can run
};

// Centralized session lifecycle manager
// - Document events create/invalidate sessions (UpdateSession, RemoveSession)
// - LSP features read sessions (GetSession)
// - Cache by URI only (not content hash) for stable typing performance
// - Concurrent requests for same URI share a single pending creation
class SessionManager {
 public:
  explicit SessionManager(
      asio::any_io_executor executor,
      std::shared_ptr<ProjectLayoutService> layout_service,
      std::shared_ptr<const GlobalCatalog> catalog,
      std::shared_ptr<OpenDocumentTracker> open_tracker,
      std::shared_ptr<spdlog::logger> logger);

  ~SessionManager();

  // Non-copyable, non-movable
  SessionManager(const SessionManager&) = delete;
  auto operator=(const SessionManager&) -> SessionManager& = delete;
  SessionManager(SessionManager&&) = delete;
  auto operator=(SessionManager&&) -> SessionManager& = delete;

  // Document event handlers (ONLY these create/invalidate sessions)
  auto UpdateSession(std::string uri, std::string content, int version)
      -> asio::awaitable<void>;

  auto InvalidateSessions(std::vector<std::string> uris) -> void;

  auto InvalidateAllSessions() -> void;  // For catalog version change

  // Cancel pending session compilation (called when document is closed)
  auto CancelPendingSession(std::string uri) -> void;

  // Updates the catalog pointer used for all future session creations
  // Must be called when GlobalCatalog is rebuilt (e.g., config changes)
  auto UpdateCatalog(std::shared_ptr<const GlobalCatalog> catalog) -> void;

  // Feature accessors (read-only)
  // Returns compilation state after Phase 1 (fast path for diagnostics)
  auto GetCompilationState(std::string uri)
      -> asio::awaitable<std::optional<CompilationState>>;

  // Returns fully-indexed session (waits for indexing to complete)
  auto GetSession(std::string uri)
      -> asio::awaitable<std::shared_ptr<const OverlaySession>>;

 private:
  // Pending session creation - concurrent requests share the same events
  struct PendingCreation {
    // Phase 1: Elaboration complete (diagnostics can proceed)
    utils::BroadcastEvent compilation_ready;
    // Phase 2: Indexing complete (symbols/definitions can proceed)
    utils::BroadcastEvent session_ready;
    int version;                         // LSP document version
    std::atomic<bool> cancelled{false};  // Lock-free cancellation flag

    explicit PendingCreation(asio::any_io_executor executor, int doc_version);
  };

  auto StartSessionCreation(std::string uri, std::string content, int version)
      -> std::shared_ptr<PendingCreation>;

  // LRU cache management helpers
  auto UpdateAccessOrder(const std::string& uri) -> void;
  auto EvictOldestIfNeeded() -> void;

  // Cache entry with version and phase tracking
  struct CacheEntry {
    std::shared_ptr<OverlaySession> session;
    int version;
    SessionPhase phase;
  };

  // Dependencies
  asio::any_io_executor executor_;
  std::shared_ptr<ProjectLayoutService> layout_service_;
  std::shared_ptr<const GlobalCatalog> catalog_;
  std::shared_ptr<OpenDocumentTracker> open_tracker_;
  std::shared_ptr<spdlog::logger> logger_;

  // Strand for thread-safe session map access
  asio::strand<asio::any_io_executor> session_strand_;

  // Protected by session_strand_:
  std::unordered_map<std::string, CacheEntry> active_sessions_;

  std::unordered_map<std::string, std::shared_ptr<PendingCreation>>
      pending_sessions_;

  // LRU tracking for cache eviction (most recently used first)
  std::vector<std::string> access_order_;
  static constexpr size_t kMaxCacheSize = 8;

  // Background compilation pool
  std::unique_ptr<asio::thread_pool> compilation_pool_;
};

}  // namespace slangd::services

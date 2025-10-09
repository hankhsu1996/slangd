#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/experimental/channel.hpp>
#include <asio/thread_pool.hpp>
#include <slang/ast/Compilation.h>
#include <slang/text/SourceManager.h>
#include <spdlog/spdlog.h>

#include "slangd/core/project_layout_service.hpp"
#include "slangd/services/global_catalog.hpp"
#include "slangd/services/overlay_session.hpp"

namespace slangd::services {

// Intermediate state after Phase 1 (elaboration) - used for fast diagnostics
struct CompilationState {
  std::shared_ptr<slang::SourceManager> source_manager;
  std::shared_ptr<slang::ast::Compilation> compilation;
  slang::BufferID main_buffer_id;
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

  auto RemoveSession(std::string uri) -> void;

  auto InvalidateSessions(std::vector<std::string> uris) -> void;

  auto InvalidateAllSessions() -> void;  // For catalog version change

  // Feature accessors (read-only)
  // Returns compilation state after Phase 1 (fast path for diagnostics)
  auto GetCompilationState(std::string uri)
      -> asio::awaitable<std::optional<CompilationState>>;

  // Returns fully-indexed session (waits for indexing to complete)
  auto GetSession(std::string uri)
      -> asio::awaitable<std::shared_ptr<const OverlaySession>>;

 private:
  // Pending session creation - concurrent requests share the same channel
  struct PendingCreation {
    using CompilationChannel =
        asio::experimental::channel<void(std::error_code, CompilationState)>;
    using SessionChannel = asio::experimental::channel<void(
        std::error_code, std::shared_ptr<OverlaySession>)>;

    // Signal: Phase 1 complete (after elaboration) - diagnostics can proceed
    std::shared_ptr<CompilationChannel> compilation_ready;
    // Signal: Phase 2 complete (after indexing) - symbols/definition can
    // proceed
    std::shared_ptr<SessionChannel> session_ready;
    // LSP document version - used to prevent race conditions
    int version;

    explicit PendingCreation(asio::any_io_executor executor, int doc_version);
  };

  auto StartSessionCreation(std::string uri, std::string content, int version)
      -> std::shared_ptr<PendingCreation>;

  // LRU cache management helpers
  auto UpdateAccessOrder(const std::string& uri) -> void;
  auto EvictOldestIfNeeded() -> void;

  // Cache entry with version tracking for intelligent cache reuse
  struct CacheEntry {
    std::shared_ptr<OverlaySession> session;
    int version;  // LSP document version
  };

  // Cache by URI with version tracking (no content_hash!)
  // Version comparison enables close/reopen optimization without rebuilding
  std::unordered_map<std::string, CacheEntry> active_sessions_;

  std::unordered_map<std::string, std::shared_ptr<PendingCreation>>
      pending_sessions_;

  // LRU tracking for cache eviction (most recently used first)
  std::vector<std::string> access_order_;
  static constexpr size_t kMaxCacheSize = 16;

  // Dependencies
  asio::any_io_executor executor_;
  std::shared_ptr<ProjectLayoutService> layout_service_;
  std::shared_ptr<const GlobalCatalog> catalog_;
  std::shared_ptr<spdlog::logger> logger_;

  // Background compilation pool
  std::unique_ptr<asio::thread_pool> compilation_pool_;
};

}  // namespace slangd::services

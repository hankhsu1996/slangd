#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>
#include <asio/experimental/channel.hpp>
#include <asio/thread_pool.hpp>
#include <slang/ast/Compilation.h>
#include <slang/text/SourceManager.h>
#include <spdlog/spdlog.h>

#include "slangd/core/project_layout_service.hpp"
#include "slangd/services/global_catalog.hpp"
#include "slangd/services/overlay_session.hpp"

namespace slangd::services {

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
  auto UpdateSession(std::string uri, std::string content)
      -> asio::awaitable<void>;

  auto RemoveSession(std::string uri) -> void;

  auto InvalidateSessions(std::vector<std::string> uris) -> void;

  auto InvalidateAllSessions() -> void;  // For catalog version change

  // Feature accessors (read-only)
  // Returns fully-indexed session (waits for session creation to complete)
  auto GetSession(std::string uri)
      -> asio::awaitable<std::shared_ptr<const OverlaySession>>;

 private:
  // Pending session creation - allows concurrent requests to wait
  struct PendingCreation {
    using SessionChannel = asio::experimental::channel<void(
        std::error_code, std::shared_ptr<OverlaySession>)>;

    std::shared_ptr<SessionChannel> session_ready;

    explicit PendingCreation(asio::any_io_executor executor);
  };

  auto StartSessionCreation(std::string uri, std::string content)
      -> std::shared_ptr<PendingCreation>;

  // Cache by URI only (no content_hash!)
  std::unordered_map<std::string, std::shared_ptr<OverlaySession>>
      active_sessions_;

  std::unordered_map<std::string, std::shared_ptr<PendingCreation>>
      pending_sessions_;

  std::unordered_set<std::string> needs_rebuild_;

  // Dependencies
  asio::any_io_executor executor_;
  std::shared_ptr<ProjectLayoutService> layout_service_;
  std::shared_ptr<const GlobalCatalog> catalog_;
  std::shared_ptr<spdlog::logger> logger_;

  // Background compilation pool
  std::unique_ptr<asio::thread_pool> compilation_pool_;
};

}  // namespace slangd::services

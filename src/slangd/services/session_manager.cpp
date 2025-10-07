#include "slangd/services/session_manager.hpp"

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/post.hpp>
#include <asio/redirect_error.hpp>
#include <asio/use_awaitable.hpp>

#include "slangd/services/overlay_session.hpp"
#include "slangd/utils/scoped_timer.hpp"

namespace slangd::services {

SessionManager::PendingCreation::PendingCreation(
    asio::any_io_executor executor, int doc_version)
    : session_ready(std::make_shared<SessionChannel>(executor, 10)),
      version(doc_version) {
}

SessionManager::SessionManager(
    asio::any_io_executor executor,
    std::shared_ptr<ProjectLayoutService> layout_service,
    std::shared_ptr<const GlobalCatalog> catalog,
    std::shared_ptr<spdlog::logger> logger)
    : executor_(executor),
      layout_service_(std::move(layout_service)),
      catalog_(std::move(catalog)),
      logger_(std::move(logger)),
      compilation_pool_(std::make_unique<asio::thread_pool>(4)) {
  logger_->debug("SessionManager initialized with 4 compilation threads");
}

SessionManager::~SessionManager() {
  logger_->debug("SessionManager shutting down");

  // Close all pending channels to signal shutdown to any active coroutines
  // NOTE: In practice, coroutines should already be complete since destructor
  // only runs when io_context has stopped. This is defensive programming.
  for (auto& [uri, pending] : pending_sessions_) {
    pending->session_ready->close();
  }

  compilation_pool_->join();
}

auto SessionManager::UpdateSession(
    std::string uri, std::string content, int version)
    -> asio::awaitable<void> {
  logger_->debug(
      "SessionManager::UpdateSession: {} (version {})", uri, version);

  // Check if we have a cached session with the same version
  if (auto it = active_sessions_.find(uri); it != active_sessions_.end()) {
    if (it->second.version == version) {
      logger_->debug(
          "SessionManager cache hit: {} (version {} unchanged)", uri, version);
      UpdateAccessOrder(uri);
      co_return;
    }
    logger_->debug(
        "SessionManager version changed: {} (old: {}, new: {})", uri,
        it->second.version, version);
    active_sessions_.erase(uri);
  }

  if (auto it = pending_sessions_.find(uri); it != pending_sessions_.end()) {
    logger_->debug(
        "Canceling pending session creation for: {} (old version {})", uri,
        it->second->version);
    it->second->session_ready->close();
    pending_sessions_.erase(it);
  }

  auto pending = StartSessionCreation(uri, content, version);
  pending_sessions_[uri] = pending;

  co_return;
}

auto SessionManager::RemoveSession(std::string uri) -> void {
  logger_->debug("SessionManager::RemoveSession: {}", uri);
  active_sessions_.erase(uri);

  // Remove from LRU tracking
  std::erase(access_order_, uri);

  if (auto it = pending_sessions_.find(uri); it != pending_sessions_.end()) {
    logger_->debug("Canceling pending session for closed document: {}", uri);
    it->second->session_ready->close();
    pending_sessions_.erase(it);
  }
}

auto SessionManager::InvalidateSessions(std::vector<std::string> uris) -> void {
  logger_->debug("SessionManager::InvalidateSessions: {} files", uris.size());

  for (const auto& uri : uris) {
    active_sessions_.erase(uri);

    // Remove from LRU tracking
    std::erase(access_order_, uri);

    if (auto it = pending_sessions_.find(uri); it != pending_sessions_.end()) {
      it->second->session_ready->close();
      pending_sessions_.erase(it);
    }
  }
}

auto SessionManager::InvalidateAllSessions() -> void {
  logger_->debug(
      "SessionManager::InvalidateAllSessions ({} active, {} pending)",
      active_sessions_.size(), pending_sessions_.size());

  active_sessions_.clear();
  access_order_.clear();

  for (auto& [uri, pending] : pending_sessions_) {
    pending->session_ready->close();
  }
  pending_sessions_.clear();
}

auto SessionManager::GetSession(std::string uri)
    -> asio::awaitable<std::shared_ptr<const OverlaySession>> {
  if (auto it = active_sessions_.find(uri); it != active_sessions_.end()) {
    logger_->debug("SessionManager::GetSession cache hit: {}", uri);
    UpdateAccessOrder(uri);
    co_return it->second.session;
  }

  if (auto it = pending_sessions_.find(uri); it != pending_sessions_.end()) {
    logger_->debug("SessionManager::GetSession waiting for pending: {}", uri);

    std::error_code ec;
    auto session = co_await it->second->session_ready->async_receive(
        asio::redirect_error(asio::use_awaitable, ec));

    if (ec) {
      logger_->error("Failed to receive session for: {}", uri);
      co_return nullptr;
    }

    co_return session;
  }

  logger_->debug("SessionManager::GetSession no session for: {}", uri);
  co_return nullptr;
}

auto SessionManager::StartSessionCreation(
    std::string uri, std::string content, int version)
    -> std::shared_ptr<PendingCreation> {
  auto pending = std::make_shared<PendingCreation>(executor_, version);

  asio::co_spawn(
      executor_,
      [this, uri, content, pending]() -> asio::awaitable<void> {
        // Single-phase: Build compilation + index + extract diagnostics
        auto session = co_await asio::co_spawn(
            compilation_pool_->get_executor(),
            [uri, content,
             this]() -> asio::awaitable<std::shared_ptr<OverlaySession>> {
              utils::ScopedTimer timer(
                  "SessionManager session creation", logger_);

              // Build compilation
              auto [source_manager, compilation, main_buffer_id] =
                  OverlaySession::BuildCompilation(
                      uri, content, layout_service_, catalog_, logger_);

              // Build semantic index (file-scoped traversal)
              auto semantic_index = semantic::SemanticIndex::FromCompilation(
                  *compilation, *source_manager, uri, catalog_.get());

              // Extract diagnostics from diagMap (populated during traversal)
              slang::Diagnostics diagnostics;
              for (const auto& diag : compilation->getAllDiagnostics()) {
                diagnostics.emplace_back(diag);
              }

              // Create session with index + diagnostics
              auto session = OverlaySession::CreateFromParts(
                  source_manager,
                  std::shared_ptr<slang::ast::Compilation>(
                      std::move(compilation)),
                  std::move(semantic_index), main_buffer_id,
                  std::move(diagnostics), logger_);

              logger_->debug(
                  "SessionManager session creation complete for {} ({})", uri,
                  utils::ScopedTimer::FormatDuration(timer.GetElapsed()));

              co_return session;
            },
            asio::use_awaitable);

        co_await asio::post(executor_, asio::use_awaitable);
        std::error_code ec;

        if (session) {
          pending->session_ready->try_send(ec, session);
          pending->session_ready->close();
          active_sessions_[uri] =
              CacheEntry{.session = session, .version = pending->version};
          UpdateAccessOrder(uri);
          EvictOldestIfNeeded();
        } else {
          pending->session_ready->close();
          logger_->error("SessionManager failed to create session: {}", uri);
        }

        // Only erase if this pending creation is still current
        if (auto it = pending_sessions_.find(uri);
            it != pending_sessions_.end() &&
            it->second->version == pending->version) {
          pending_sessions_.erase(it);
        }
      },
      asio::detached);

  return pending;
}

auto SessionManager::UpdateAccessOrder(const std::string& uri) -> void {
  // Remove existing entry if present
  std::erase(access_order_, uri);
  // Add to front (most recently used)
  access_order_.insert(access_order_.begin(), uri);
}

auto SessionManager::EvictOldestIfNeeded() -> void {
  while (active_sessions_.size() > kMaxCacheSize) {
    if (access_order_.empty()) {
      logger_->error(
          "SessionManager LRU tracking out of sync (active: {}, LRU: {})",
          active_sessions_.size(), access_order_.size());
      break;
    }

    // Evict least recently used (last in vector)
    const auto& oldest_uri = access_order_.back();
    logger_->debug(
        "SessionManager LRU eviction: {} ({}/{} entries)", oldest_uri,
        active_sessions_.size(), kMaxCacheSize);

    active_sessions_.erase(oldest_uri);
    access_order_.pop_back();
  }
}

}  // namespace slangd::services

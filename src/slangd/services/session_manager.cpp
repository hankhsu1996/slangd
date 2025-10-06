#include "slangd/services/session_manager.hpp"

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/post.hpp>
#include <asio/redirect_error.hpp>
#include <asio/use_awaitable.hpp>

#include "slangd/services/overlay_session.hpp"
#include "slangd/utils/scoped_timer.hpp"

namespace slangd::services {

SessionManager::PendingCreation::PendingCreation(asio::any_io_executor executor)
    : session_ready(std::make_shared<SessionChannel>(executor, 10)) {
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
  compilation_pool_->join();
}

auto SessionManager::UpdateSession(std::string uri, std::string content)
    -> asio::awaitable<void> {
  logger_->debug("SessionManager::UpdateSession: {}", uri);

  // Invalidate any existing session
  active_sessions_.erase(uri);

  // Cancel any pending creation
  if (auto it = pending_sessions_.find(uri); it != pending_sessions_.end()) {
    logger_->debug("Canceling pending session creation for: {}", uri);
    it->second->session_ready->close();
    pending_sessions_.erase(it);
  }

  // Start new session creation
  auto pending = StartSessionCreation(uri, content);
  pending_sessions_[uri] = pending;

  co_return;
}

auto SessionManager::RemoveSession(std::string uri) -> void {
  logger_->debug("SessionManager::RemoveSession: {}", uri);
  active_sessions_.erase(uri);
  needs_rebuild_.erase(uri);

  // Cancel any pending creation
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

    // Cancel pending creation
    if (auto it = pending_sessions_.find(uri); it != pending_sessions_.end()) {
      it->second->session_ready->close();
      pending_sessions_.erase(it);
    }

    // Mark for rebuild on next access
    needs_rebuild_.insert(uri);
  }
}

auto SessionManager::InvalidateAllSessions() -> void {
  logger_->debug(
      "SessionManager::InvalidateAllSessions ({} active, {} pending)",
      active_sessions_.size(), pending_sessions_.size());

  active_sessions_.clear();
  needs_rebuild_.clear();

  // Cancel all pending creations
  for (auto& [uri, pending] : pending_sessions_) {
    pending->session_ready->close();
  }
  pending_sessions_.clear();
}

auto SessionManager::GetSession(std::string uri)
    -> asio::awaitable<std::shared_ptr<const OverlaySession>> {
  // Check if session exists and is active
  if (auto it = active_sessions_.find(uri); it != active_sessions_.end()) {
    logger_->debug("SessionManager::GetSession cache hit: {}", uri);
    co_return it->second;
  }

  // Check if creation is pending
  if (auto it = pending_sessions_.find(uri); it != pending_sessions_.end()) {
    logger_->debug("SessionManager::GetSession waiting for pending: {}", uri);

    // Wait for session_ready
    std::error_code ec;
    auto session = co_await it->second->session_ready->async_receive(
        asio::redirect_error(asio::use_awaitable, ec));

    if (ec) {
      logger_->error("Failed to receive session for: {}", uri);
      co_return nullptr;
    }

    co_return session;
  }

  // No session available
  logger_->debug("SessionManager::GetSession no session for: {}", uri);
  co_return nullptr;
}

auto SessionManager::StartSessionCreation(std::string uri, std::string content)
    -> std::shared_ptr<PendingCreation> {
  auto pending = std::make_shared<PendingCreation>(executor_);

  // Start session creation in background
  asio::co_spawn(
      executor_,
      [this, uri, content, pending]() -> asio::awaitable<void> {
        // Dispatch to compilation pool
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

              // Build semantic index
              auto semantic_index = semantic::SemanticIndex::FromCompilation(
                  *compilation, *source_manager, uri, catalog_.get());

              // Create session from parts
              auto session = OverlaySession::CreateFromParts(
                  std::move(source_manager), std::move(compilation),
                  std::move(semantic_index), main_buffer_id, logger_);

              auto elapsed = timer.GetElapsed();
              logger_->debug(
                  "SessionManager created session for {} ({})", uri,
                  utils::ScopedTimer::FormatDuration(elapsed));

              co_return session;
            },
            asio::use_awaitable);

        // Post back to main strand
        co_await asio::post(executor_, asio::use_awaitable);

        if (session) {
          // Signal session_ready
          std::error_code ec;
          pending->session_ready->try_send(ec, session);
          pending->session_ready->close();

          // Add to active sessions
          active_sessions_[uri] = session;
          logger_->debug(
              "SessionManager added session to cache: {} ({} active)", uri,
              active_sessions_.size());
        } else {
          // Signal failure
          pending->session_ready->close();
          logger_->error("SessionManager failed to create session: {}", uri);
        }

        // Remove from pending
        pending_sessions_.erase(uri);
      },
      asio::detached);

  return pending;
}

}  // namespace slangd::services

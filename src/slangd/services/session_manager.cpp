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
    : compilation_ready(std::make_shared<CompilationChannel>(executor, 10)),
      session_ready(std::make_shared<SessionChannel>(executor, 10)),
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
  compilation_pool_->join();
}

auto SessionManager::UpdateSession(
    std::string uri, std::string content, int version)
    -> asio::awaitable<void> {
  logger_->debug(
      "SessionManager::UpdateSession: {} (version {})", uri, version);

  active_sessions_.erase(uri);

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
  needs_rebuild_.erase(uri);

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

    if (auto it = pending_sessions_.find(uri); it != pending_sessions_.end()) {
      it->second->session_ready->close();
      pending_sessions_.erase(it);
    }

    needs_rebuild_.insert(uri);
  }
}

auto SessionManager::InvalidateAllSessions() -> void {
  logger_->debug(
      "SessionManager::InvalidateAllSessions ({} active, {} pending)",
      active_sessions_.size(), pending_sessions_.size());

  active_sessions_.clear();
  needs_rebuild_.clear();

  for (auto& [uri, pending] : pending_sessions_) {
    pending->session_ready->close();
  }
  pending_sessions_.clear();
}

auto SessionManager::GetSession(std::string uri)
    -> asio::awaitable<std::shared_ptr<const OverlaySession>> {
  if (auto it = active_sessions_.find(uri); it != active_sessions_.end()) {
    logger_->debug("SessionManager::GetSession cache hit: {}", uri);
    co_return it->second;
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

auto SessionManager::GetSessionForDiagnostics(std::string uri)
    -> asio::awaitable<CompilationState> {
  if (auto it = pending_sessions_.find(uri); it != pending_sessions_.end()) {
    logger_->debug(
        "SessionManager::GetSessionForDiagnostics waiting for compilation: {}",
        uri);

    std::error_code ec;
    auto state = co_await it->second->compilation_ready->async_receive(
        asio::redirect_error(asio::use_awaitable, ec));

    if (ec) {
      logger_->error("Failed to receive compilation for: {}", uri);
      co_return CompilationState{};
    }

    co_return state;
  }

  logger_->debug(
      "SessionManager::GetSessionForDiagnostics no pending for: {}", uri);
  co_return CompilationState{};
}

auto SessionManager::StartSessionCreation(
    std::string uri, std::string content, int version)
    -> std::shared_ptr<PendingCreation> {
  auto pending = std::make_shared<PendingCreation>(executor_, version);

  asio::co_spawn(
      executor_,
      [this, uri, content, pending]() -> asio::awaitable<void> {
        auto compilation_state = co_await asio::co_spawn(
            compilation_pool_->get_executor(),
            [uri, content, this]() -> asio::awaitable<CompilationState> {
              utils::ScopedTimer timer("SessionManager compilation", logger_);

              auto [source_manager, compilation, main_buffer_id] =
                  OverlaySession::BuildCompilation(
                      uri, content, layout_service_, catalog_, logger_);

              const auto& diag_engine = compilation->getAllDiagnostics();
              slang::Diagnostics diagnostics;
              for (const auto& diag : diag_engine) {
                diagnostics.emplace_back(diag);
              }

              logger_->debug(
                  "SessionManager compilation complete for {} ({})", uri,
                  utils::ScopedTimer::FormatDuration(timer.GetElapsed()));

              co_return CompilationState{
                  .source_manager = source_manager,
                  .compilation = std::shared_ptr<slang::ast::Compilation>(
                      std::move(compilation)),
                  .buffer_id = main_buffer_id,
                  .diagnostics = std::move(diagnostics)};
            },
            asio::use_awaitable);

        co_await asio::post(executor_, asio::use_awaitable);
        std::error_code ec;
        pending->compilation_ready->try_send(ec, compilation_state);
        pending->compilation_ready->close();

        auto session = co_await asio::co_spawn(
            compilation_pool_->get_executor(),
            [uri, compilation_state = std::move(compilation_state),
             this]() mutable
                -> asio::awaitable<std::shared_ptr<OverlaySession>> {
              utils::ScopedTimer timer("SessionManager indexing", logger_);

              auto semantic_index = semantic::SemanticIndex::FromCompilation(
                  *compilation_state.compilation,
                  *compilation_state.source_manager, uri, catalog_.get());

              auto session = OverlaySession::CreateFromParts(
                  compilation_state.source_manager,
                  compilation_state.compilation, std::move(semantic_index),
                  compilation_state.buffer_id, logger_);

              logger_->debug(
                  "SessionManager indexing complete for {} ({})", uri,
                  utils::ScopedTimer::FormatDuration(timer.GetElapsed()));

              co_return session;
            },
            asio::use_awaitable);

        co_await asio::post(executor_, asio::use_awaitable);

        if (session) {
          pending->session_ready->try_send(ec, session);
          pending->session_ready->close();

          active_sessions_[uri] = session;
          logger_->debug(
              "SessionManager added session to cache: {} version {} ({} "
              "active)",
              uri, pending->version, active_sessions_.size());
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

}  // namespace slangd::services

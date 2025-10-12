#include "slangd/services/session_manager.hpp"

#include <ranges>

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
    : compilation_ready(executor),
      session_ready(executor),
      version(doc_version) {
}

SessionManager::SessionManager(
    asio::any_io_executor executor,
    std::shared_ptr<ProjectLayoutService> layout_service,
    std::shared_ptr<const GlobalCatalog> catalog,
    std::shared_ptr<OpenDocumentTracker> open_tracker,
    std::shared_ptr<spdlog::logger> logger)
    : executor_(executor),
      layout_service_(std::move(layout_service)),
      catalog_(std::move(catalog)),
      open_tracker_(std::move(open_tracker)),
      logger_(std::move(logger)),
      session_strand_(asio::make_strand(executor)),
      compilation_pool_(std::make_unique<asio::thread_pool>(4)) {
}

SessionManager::~SessionManager() {
  compilation_pool_->join();
}

auto SessionManager::UpdateSession(
    std::string uri, std::string content, int version)
    -> asio::awaitable<void> {
  co_await asio::post(session_strand_, asio::use_awaitable);

  logger_->debug(
      "SessionManager::UpdateSession: {} (version {})", uri, version);

  // Check if we have a cached session with the same version
  if (auto it = active_sessions_.find(uri); it != active_sessions_.end()) {
    if (it->second.version == version) {
      UpdateAccessOrder(uri);
      co_return;
    }
    logger_->debug(
        "SessionManager version changed: {} (old: {}, new: {})", uri,
        it->second.version, version);
    active_sessions_.erase(uri);
  }

  // Check if pending session already exists
  if (auto it = pending_sessions_.find(uri); it != pending_sessions_.end()) {
    if (it->second->version == version) {
      co_return;  // Same version, reuse pending session
    } else {
      // Different version - cancel old and create new
      logger_->info(
          "SessionManager: Cancelling pending session for {} (old version {}, "
          "new version {})",
          uri, it->second->version, version);
      pending_sessions_.erase(it);
    }
  }

  auto new_pending = StartSessionCreation(uri, content, version);
  pending_sessions_[uri] = new_pending;

  co_return;
}

auto SessionManager::InvalidateSessions(std::vector<std::string> uris) -> void {
  asio::co_spawn(
      executor_,
      [this, uris = std::move(uris)]() -> asio::awaitable<void> {
        co_await asio::post(session_strand_, asio::use_awaitable);

        for (const auto& uri : uris) {
          active_sessions_.erase(uri);
          std::erase(access_order_, uri);
          pending_sessions_.erase(uri);
          logger_->debug("SessionManager::InvalidateSessions: {}", uri);
        }
        co_return;
      },
      asio::detached);
}

auto SessionManager::InvalidateAllSessions() -> void {
  asio::co_spawn(
      executor_,
      [this]() -> asio::awaitable<void> {
        co_await asio::post(session_strand_, asio::use_awaitable);

        logger_->debug(
            "SessionManager::InvalidateAllSessions ({} active, {} pending)",
            active_sessions_.size(), pending_sessions_.size());

        active_sessions_.clear();
        access_order_.clear();
        pending_sessions_.clear();
        co_return;
      },
      asio::detached);
}

auto SessionManager::CancelPendingSession(std::string uri) -> void {
  asio::co_spawn(
      executor_,
      [this, uri]() -> asio::awaitable<void> {
        co_await asio::post(session_strand_, asio::use_awaitable);

        if (auto it = pending_sessions_.find(uri);
            it != pending_sessions_.end()) {
          pending_sessions_.erase(it);
          logger_->debug(
              "Cancelled pending session for: {} (active={}, pending={})", uri,
              active_sessions_.size(), pending_sessions_.size());
        }

        co_return;
      },
      asio::detached);
}

auto SessionManager::UpdateCatalog(std::shared_ptr<const GlobalCatalog> catalog)
    -> void {
  asio::co_spawn(
      executor_,
      [this, catalog]() -> asio::awaitable<void> {
        co_await asio::post(session_strand_, asio::use_awaitable);

        logger_->debug(
            "SessionManager::UpdateCatalog: Updating catalog pointer (old "
            "version: {}, new version: {})",
            catalog_ ? catalog_->GetVersion() : 0,
            catalog ? catalog->GetVersion() : 0);

        catalog_ = catalog;

        co_return;
      },
      asio::detached);
}

auto SessionManager::GetCompilationState(std::string uri)
    -> asio::awaitable<std::optional<CompilationState>> {
  co_await asio::post(session_strand_, asio::use_awaitable);

  // Fast path: Check cache
  if (auto it = active_sessions_.find(uri); it != active_sessions_.end()) {
    if (it->second.phase >= SessionPhase::kElaborationComplete) {
      UpdateAccessOrder(uri);
      auto& session = it->second.session;
      co_return CompilationState{
          .source_manager = session->GetSourceManagerPtr(),
          .compilation = session->GetCompilationPtr(),
          .main_buffer_id = session->GetMainBufferID()};
    }
  }

  // Slow path: Wait for Phase 1 completion
  if (auto it = pending_sessions_.find(uri); it != pending_sessions_.end()) {
    logger_->debug(
        "SessionManager::GetCompilationState waiting for compilation_ready: {}",
        uri);

    auto pending = it->second;
    co_await pending->compilation_ready.AsyncWait(asio::use_awaitable);
    co_await asio::post(session_strand_, asio::use_awaitable);

    if (auto cache_it = active_sessions_.find(uri);
        cache_it != active_sessions_.end() &&
        cache_it->second.phase >= SessionPhase::kElaborationComplete) {
      UpdateAccessOrder(uri);
      auto& session = cache_it->second.session;
      co_return CompilationState{
          .source_manager = session->GetSourceManagerPtr(),
          .compilation = session->GetCompilationPtr(),
          .main_buffer_id = session->GetMainBufferID()};
    }

    logger_->info(
        "GetCompilationState: Session not found for {} after notification "
        "(evicted or cancelled)",
        uri);
    co_return std::nullopt;
  }

  logger_->info(
      "SessionManager::GetCompilationState no session found: {}", uri);
  co_return std::nullopt;
}

auto SessionManager::GetSession(std::string uri)
    -> asio::awaitable<std::shared_ptr<const OverlaySession>> {
  co_await asio::post(session_strand_, asio::use_awaitable);

  // Fast path: Check cache
  if (auto it = active_sessions_.find(uri); it != active_sessions_.end()) {
    if (it->second.phase >= SessionPhase::kIndexingComplete) {
      UpdateAccessOrder(uri);
      co_return it->second.session;
    }
  }

  // Slow path: Wait for Phase 2 completion
  if (auto it = pending_sessions_.find(uri); it != pending_sessions_.end()) {
    logger_->debug("SessionManager::GetSession waiting for pending: {}", uri);

    auto pending = it->second;
    co_await pending->session_ready.AsyncWait(asio::use_awaitable);
    co_await asio::post(session_strand_, asio::use_awaitable);

    if (auto cache_it = active_sessions_.find(uri);
        cache_it != active_sessions_.end() &&
        cache_it->second.phase >= SessionPhase::kIndexingComplete) {
      UpdateAccessOrder(uri);
      co_return cache_it->second.session;
    }

    logger_->info(
        "GetSession: Session not found for {} after notification (evicted or "
        "cancelled)",
        uri);
    co_return nullptr;
  }

  logger_->info("SessionManager::GetSession no session found: {}", uri);
  co_return nullptr;
}

auto SessionManager::StartSessionCreation(
    std::string uri, std::string content, int version)
    -> std::shared_ptr<PendingCreation> {
  auto pending = std::make_shared<PendingCreation>(executor_, version);

  asio::co_spawn(
      executor_,
      [this, uri, content, pending]() -> asio::awaitable<void> {
        auto result = co_await asio::co_spawn(
            compilation_pool_->get_executor(),
            [uri, content, this, pending]()
                -> asio::awaitable<
                    std::optional<std::shared_ptr<OverlaySession>>> {
              utils::ScopedTimer timer(
                  "SessionManager session creation", logger_);

              auto [source_manager, compilation, main_buffer_id] =
                  OverlaySession::BuildCompilation(
                      uri, content, layout_service_, catalog_, logger_);

              auto semantic_index = semantic::SemanticIndex::FromCompilation(
                  *compilation, *source_manager, uri, catalog_.get(), logger_);

              // Check if still current before storing
              co_await asio::post(session_strand_, asio::use_awaitable);

              auto it = pending_sessions_.find(uri);
              if (it == pending_sessions_.end() ||
                  it->second->version != pending->version) {
                logger_->debug(
                    "Session creation cancelled after elaboration: {}", uri);
                co_return std::nullopt;
              }

              // Store Phase 1 in cache, then broadcast
              auto compilation_shared =
                  std::shared_ptr<slang::ast::Compilation>(
                      std::move(compilation));
              auto partial_session = OverlaySession::CreateFromParts(
                  source_manager, compilation_shared, std::move(semantic_index),
                  main_buffer_id, logger_);

              active_sessions_[uri] = CacheEntry{
                  .session = partial_session,
                  .version = pending->version,
                  .phase = SessionPhase::kElaborationComplete};
              UpdateAccessOrder(uri);
              EvictOldestIfNeeded();

              pending->compilation_ready.Set();

              logger_->debug(
                  "SessionManager Phase 1 complete (elaboration): {}", uri);

              co_return partial_session;
            },
            asio::use_awaitable);

        co_await asio::post(session_strand_, asio::use_awaitable);

        if (result.has_value() && result.value()) {
          auto it = pending_sessions_.find(uri);
          if (it != pending_sessions_.end() &&
              it->second->version == pending->version) {
            // Upgrade to Phase 2, then broadcast
            if (auto cache_it = active_sessions_.find(uri);
                cache_it != active_sessions_.end()) {
              cache_it->second.phase = SessionPhase::kIndexingComplete;
              pending->session_ready.Set();
              logger_->debug(
                  "SessionManager Phase 2 complete (indexing): {}", uri);
            }
            pending_sessions_.erase(it);
          } else {
            logger_->debug(
                "Session creation superseded during Phase 2: {}", uri);
          }
        } else {
          logger_->debug("Session creation failed or cancelled: {}", uri);
          auto it = pending_sessions_.find(uri);
          if (it != pending_sessions_.end() &&
              it->second->version == pending->version) {
            pending_sessions_.erase(it);
          }
        }
      },
      asio::detached);

  return pending;
}

auto SessionManager::UpdateAccessOrder(const std::string& uri) -> void {
  std::erase(access_order_, uri);
  access_order_.insert(access_order_.begin(), uri);  // MRU at front
}

auto SessionManager::EvictOldestIfNeeded() -> void {
  while (active_sessions_.size() > kMaxCacheSize) {
    if (access_order_.empty()) {
      logger_->error(
          "SessionManager LRU tracking out of sync (active: {}, LRU: {})",
          active_sessions_.size(), access_order_.size());
      break;
    }

    // Smart eviction: prefer closed files over open files
    std::string uri_to_evict;
    bool found_closed = false;

    for (const auto& uri : access_order_ | std::views::reverse) {
      if (!open_tracker_->Contains(uri)) {
        uri_to_evict = uri;
        found_closed = true;
        logger_->debug(
            "SessionManager evicting closed file: {} ({}/{} entries)",
            uri_to_evict, active_sessions_.size(), kMaxCacheSize);
        break;
      }
    }

    if (!found_closed) {
      uri_to_evict = access_order_.back();
      logger_->debug(
          "SessionManager evicting open file (no closed files available): {} "
          "({}/{} entries)",
          uri_to_evict, active_sessions_.size(), kMaxCacheSize);
    }

    active_sessions_.erase(uri_to_evict);
    std::erase(access_order_, uri_to_evict);
  }
}

}  // namespace slangd::services

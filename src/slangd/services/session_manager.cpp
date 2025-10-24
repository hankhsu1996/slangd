#include "slangd/services/session_manager.hpp"

#include <mimalloc.h>
#include <thread>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/post.hpp>
#include <asio/redirect_error.hpp>
#include <asio/use_awaitable.hpp>

#include "slangd/services/overlay_session.hpp"
#include "slangd/utils/memory_utils.hpp"

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
    std::shared_ptr<const PreambleManager> preamble_manager,
    std::shared_ptr<OpenDocumentTracker> open_tracker,
    std::shared_ptr<spdlog::logger> logger)
    : executor_(executor),
      logger_(std::move(logger)),
      layout_service_(std::move(layout_service)),
      preamble_manager_(std::move(preamble_manager)),
      open_tracker_(std::move(open_tracker)),
      session_strand_(asio::make_strand(executor)),
      compilation_pool_(std::make_unique<asio::thread_pool>([] {
        const auto hw_threads = std::thread::hardware_concurrency();
        const auto num_threads = std::min(hw_threads / 2, 16U);
        return num_threads == 0 ? 1U : num_threads;
      }())),
      overlay_strand_(asio::make_strand(compilation_pool_->get_executor())) {
  // Multi-threaded pool enables parallel preamble syntax tree parsing
  // overlay_strand_ serializes overlay elaboration (prevents concurrent
  // preamble access - Slang compilation is not thread-safe)
}

SessionManager::~SessionManager() {
  compilation_pool_->join();
}

auto SessionManager::UpdateSession(
    std::string uri, std::string content, int version,
    std::optional<CompilationReadyHook> on_compilation_ready,
    std::optional<SessionReadyHook> on_session_ready) -> asio::awaitable<void> {
  co_await asio::post(session_strand_, asio::use_awaitable);

  // Capture shared_ptr snapshots to pass to background thread pool
  // Prevents data race when UpdatePreambleManager swaps these pointers
  auto preamble = preamble_manager_;
  auto layout = layout_service_;

  logger_->debug("Session update: {} (version {})", uri, version);

  // Cancel cleanup timer if exists (file reopened within cleanup window)
  if (auto timer_it = cleanup_timers_.find(uri);
      timer_it != cleanup_timers_.end()) {
    timer_it->second->cancel();
    cleanup_timers_.erase(timer_it);
    logger_->debug("Cancelled cleanup timer for reopened file: {}", uri);
  }

  // Check if we have a stored session with the same version
  if (auto it = sessions_.find(uri); it != sessions_.end()) {
    if (it->second.version == version) {
      co_return;
    }
    logger_->debug(
        "SessionManager version changed: {} (old: {}, new: {})", uri,
        it->second.version, version);
    sessions_.erase(uri);
  }

  // Check if pending session already exists
  if (auto it = pending_.find(uri); it != pending_.end()) {
    if (it->second->version == version) {
      co_return;  // Same version, reuse pending session
    } else {
      // Different version - cancel old and create new
      logger_->info(
          "SessionManager: Cancelling pending session for {} (old version {}, "
          "new version {})",
          uri, it->second->version, version);
      it->second->cancelled.store(true, std::memory_order_release);
      pending_.erase(it);
    }
  }

  auto new_pending = StartSessionCreation(
      uri, content, version, preamble, layout, on_compilation_ready,
      on_session_ready);
  pending_[uri] = new_pending;

  co_return;
}

auto SessionManager::InvalidateSessions(std::vector<std::string> uris) -> void {
  asio::co_spawn(
      executor_,
      [this, uris = std::move(uris)]() -> asio::awaitable<void> {
        co_await asio::post(session_strand_, asio::use_awaitable);

        for (const auto& uri : uris) {
          if (auto it = pending_.find(uri); it != pending_.end()) {
            it->second->cancelled.store(true, std::memory_order_release);
          }
          // Cancel cleanup timer if exists
          if (auto timer_it = cleanup_timers_.find(uri);
              timer_it != cleanup_timers_.end()) {
            timer_it->second->cancel();
            cleanup_timers_.erase(timer_it);
          }
          sessions_.erase(uri);
          pending_.erase(uri);
          logger_->debug("Session invalidated: {}", uri);
        }
        co_return;
      },
      asio::detached);
}

auto SessionManager::CancelPendingSession(std::string uri) -> void {
  asio::co_spawn(
      executor_,
      [this, uri]() -> asio::awaitable<void> {
        co_await asio::post(session_strand_, asio::use_awaitable);

        if (auto it = pending_.find(uri); it != pending_.end()) {
          it->second->cancelled.store(true, std::memory_order_release);
          pending_.erase(it);
          logger_->debug(
              "Cancelled pending session for: {} (stored={}, pending={})", uri,
              sessions_.size(), pending_.size());
        }

        co_return;
      },
      asio::detached);
}

auto SessionManager::UpdatePreambleManager(
    std::shared_ptr<const PreambleManager> preamble_manager)
    -> asio::awaitable<void> {
  // Execute on strand immediately (no queueing with co_spawn)
  // This prevents multiple shared_ptr copies being held in queued lambdas
  co_await asio::post(session_strand_, asio::use_awaitable);

  auto before_mb = utils::GetRssMB();
  preamble_manager_ = std::move(preamble_manager);

  // Force mimalloc to return unused memory pages to OS
  // Note: old preamble may still be held by existing sessions
  mi_collect(true);

  auto after_mb = utils::GetRssMB();
  auto freed_mb = before_mb > after_mb ? before_mb - after_mb : 0;
  logger_->debug(
      "Preamble swap: {} MB -> {} MB (freed {} MB)", before_mb, after_mb,
      freed_mb);
}

auto SessionManager::InvalidateAllSessions() -> asio::awaitable<void> {
  // Execute on strand immediately (no queueing with co_spawn)
  // This ensures old sessions are destroyed before proceeding
  co_await asio::post(session_strand_, asio::use_awaitable);

  logger_->debug(
      "Invalidating all sessions ({} stored, {} pending, {} active tasks)",
      sessions_.size(), pending_.size(), active_task_completions_.size());

  // Cancel all pending session creations
  for (auto& [uri, pending] : pending_) {
    pending->cancelled.store(true, std::memory_order_release);
  }

  // Cancel all cleanup timers
  for (auto& [uri, timer] : cleanup_timers_) {
    timer->cancel();
  }

  // Wait for all active session tasks to complete and release preamble captures
  // Move completions out to avoid concurrent modification
  std::vector<std::shared_ptr<TaskCompletion>> completions_to_await;
  std::swap(completions_to_await, active_task_completions_);

  logger_->debug(
      "Waiting for {} active tasks to drain", completions_to_await.size());
  for (auto& completion : completions_to_await) {
    co_await completion->complete.AsyncWait(asio::use_awaitable);
  }
  logger_->debug("All active tasks drained");

  // Now safe to clear maps - all lambdas have released their captures
  auto before_mb = utils::GetRssMB();
  sessions_.clear();
  pending_.clear();
  cleanup_timers_.clear();

  // Force mimalloc to return unused memory pages to OS
  mi_collect(true);

  auto after_mb = utils::GetRssMB();
  auto freed_mb = before_mb > after_mb ? before_mb - after_mb : 0;
  logger_->debug(
      "Cleared sessions: {} MB -> {} MB (freed {} MB)", before_mb, after_mb,
      freed_mb);
}

auto SessionManager::StartSessionCreation(
    std::string uri, std::string content, int version,
    std::shared_ptr<const PreambleManager> preamble_manager,
    std::shared_ptr<ProjectLayoutService> layout_service,
    std::optional<CompilationReadyHook> on_compilation_ready,
    std::optional<SessionReadyHook> on_session_ready)
    -> std::shared_ptr<PendingCreation> {
  auto pending = std::make_shared<PendingCreation>(executor_, version);
  pending->on_compilation_ready = std::move(on_compilation_ready);
  pending->on_session_ready = std::move(on_session_ready);

  // Track completion for preamble rebuild coordination
  // Must be on strand (called from UpdateSession which is on strand)
  auto completion = std::make_shared<TaskCompletion>(executor_);
  active_task_completions_.push_back(completion);

  // Spawn session creation with detached - runs immediately
  asio::co_spawn(
      executor_,
      [this, uri, content, pending, preamble_manager, layout_service,
       completion]() -> asio::awaitable<void> {
        auto result = co_await asio::co_spawn(
            overlay_strand_,
            [uri, content, this, pending, preamble_manager, layout_service]()
                -> asio::awaitable<
                    std::optional<std::shared_ptr<OverlaySession>>> {
              // Check cancellation flag (lock-free, stays on pool thread)
              if (pending->cancelled.load(std::memory_order_acquire)) {
                logger_->debug(
                    "Session creation cancelled before compilation: {}", uri);
                co_return std::nullopt;
              }

              auto [source_manager, compilation, main_buffer_id] =
                  OverlaySession::BuildCompilation(
                      uri, content, layout_service, preamble_manager, logger_);

              // Check again after expensive BuildCompilation
              if (pending->cancelled.load(std::memory_order_acquire)) {
                logger_->debug(
                    "Session creation cancelled after compilation: {}", uri);
                co_return std::nullopt;
              }

              auto result = semantic::SemanticIndex::FromCompilation(
                  *compilation, *source_manager, uri, main_buffer_id,
                  preamble_manager.get(), logger_);

              if (!result) {
                logger_->error(
                    "Semantic indexing failed for '{}': {}", uri,
                    result.error());
                // Return nullopt - session creation failed
                co_return std::nullopt;
              }

              auto semantic_index = std::move(*result);

              // Switch to strand to check pending_ map and store results
              // (shared state requires strand protection)
              co_await asio::post(session_strand_, asio::use_awaitable);

              auto it = pending_.find(uri);
              if (it == pending_.end() ||
                  it->second->version != pending->version ||
                  it->second->cancelled.load(std::memory_order_acquire)) {
                logger_->debug(
                    "Session creation cancelled after elaboration: {}", uri);
                co_return std::nullopt;
              }

              // Store Phase 1, then broadcast
              auto compilation_shared =
                  std::shared_ptr<slang::ast::Compilation>(
                      std::move(compilation));
              auto partial_session = OverlaySession::CreateFromParts(
                  source_manager, compilation_shared, std::move(semantic_index),
                  main_buffer_id, logger_, preamble_manager);

              sessions_[uri] = SessionEntry{
                  .session = partial_session,
                  .version = pending->version,
                  .phase = SessionPhase::kElaborationComplete};

              // Execute Phase 1 hook if provided (on strand, session cannot be
              // cleaned up)
              if (pending->on_compilation_ready) {
                CompilationState state{
                    .compilation = partial_session->GetCompilationPtr(),
                    .main_buffer_id = partial_session->GetMainBufferID()};
                (*pending->on_compilation_ready)(state);
              }

              pending->compilation_ready.Set();
              co_return partial_session;
            },
            asio::use_awaitable);

        // Return to strand for Phase 2 storage (session upgrade)
        co_await asio::post(session_strand_, asio::use_awaitable);

        bool session_ready_signaled = false;

        if (result.has_value() && result.value()) {
          auto it = pending_.find(uri);
          if (it != pending_.end() && it->second->version == pending->version) {
            // Upgrade to Phase 2, then broadcast
            if (auto session_it = sessions_.find(uri);
                session_it != sessions_.end()) {
              session_it->second.phase = SessionPhase::kIndexingComplete;

              // Execute Phase 2 hook if provided (on strand, session cannot be
              // cleaned up)
              if (pending->on_session_ready) {
                (*pending->on_session_ready)(*session_it->second.session);
              }

              pending->session_ready.Set();
              session_ready_signaled = true;
            }
            pending_.erase(it);
          } else {
            logger_->debug(
                "Session creation superseded during Phase 2: {}", uri);
          }
        } else {
          logger_->warn("Session creation failed or cancelled for: {}", uri);
        }

        // Always signal session_ready if not already signaled
        // This ensures waiters don't hang forever on failure/cancellation
        if (!session_ready_signaled) {
          // If inner coroutine failed before Phase 1, signal compilation_ready
          // too
          if (!result.has_value() || !result.value()) {
            pending->compilation_ready.Set();
          }
          pending->session_ready.Set();

          // Clean up pending_ entry if it still points to this version
          auto it = pending_.find(uri);
          if (it != pending_.end() && it->second->version == pending->version) {
            pending_.erase(it);
          }
        }

        // Signal completion (success or failure)
        completion->complete.Set();
      },
      asio::detached);

  return pending;
}

auto SessionManager::ScheduleCleanup(std::string uri) -> void {
  asio::co_spawn(
      executor_,
      [this, uri]() -> asio::awaitable<void> {
        co_await asio::post(session_strand_, asio::use_awaitable);

        // Cancel existing timer if any
        if (auto it = cleanup_timers_.find(uri); it != cleanup_timers_.end()) {
          it->second->cancel();
          cleanup_timers_.erase(it);
        }

        // Create new timer for cleanup delay
        auto timer =
            std::make_unique<asio::steady_timer>(executor_, kCleanupDelay);
        auto* timer_ptr = timer.get();
        cleanup_timers_[uri] = std::move(timer);

        timer_ptr->async_wait([this, uri](std::error_code ec) {
          if (!ec) {
            // Timer expired - remove session
            asio::co_spawn(
                executor_,
                [this, uri]() -> asio::awaitable<void> {
                  co_await asio::post(session_strand_, asio::use_awaitable);

                  // Only remove if timer wasn't replaced by UpdateSession
                  // Prevents TOCTOU race where UpdateSession cancels timer
                  // while callback is firing
                  if (auto timer_it = cleanup_timers_.find(uri);
                      timer_it != cleanup_timers_.end()) {
                    sessions_.erase(uri);
                    cleanup_timers_.erase(uri);

                    logger_->debug(
                        "Session removed after cleanup delay: {} (stored={})",
                        uri, sessions_.size());
                  }

                  co_return;
                },
                asio::detached);
          } else if (ec != asio::error::operation_aborted) {
            // Log unexpected errors (ignore cancellation)
            logger_->warn("Cleanup timer error for {}: {}", uri, ec.message());
          }
        });

        co_return;
      },
      asio::detached);
}

}  // namespace slangd::services

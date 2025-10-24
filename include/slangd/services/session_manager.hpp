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
#include "slangd/services/open_document_tracker.hpp"
#include "slangd/services/overlay_session.hpp"
#include "slangd/services/preamble_manager.hpp"
#include "slangd/utils/broadcast_event.hpp"

namespace slangd::services {

// Intermediate state after Phase 1 (elaboration) - used for fast diagnostics
struct CompilationState {
  std::shared_ptr<slang::ast::Compilation> compilation;
  slang::BufferID main_buffer_id;
};

// Hook types for extracting data during session creation (before caching)
// Hooks execute on background thread where compilation completes
using CompilationReadyHook = std::function<void(const CompilationState&)>;
using SessionReadyHook = std::function<void(const OverlaySession&)>;

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
      std::shared_ptr<const PreambleManager> preamble_manager,
      std::shared_ptr<OpenDocumentTracker> open_tracker,
      std::shared_ptr<spdlog::logger> logger);

  ~SessionManager();

  // Non-copyable, non-movable
  SessionManager(const SessionManager&) = delete;
  auto operator=(const SessionManager&) -> SessionManager& = delete;
  SessionManager(SessionManager&&) = delete;
  auto operator=(SessionManager&&) -> SessionManager& = delete;

  // Document event handlers (ONLY these create/invalidate sessions)
  // Optional hooks execute during session creation (before caching) - useful
  // for server-push features like diagnostics that need guaranteed execution
  auto UpdateSession(
      std::string uri, std::string content, int version,
      std::optional<CompilationReadyHook> on_compilation_ready = std::nullopt,
      std::optional<SessionReadyHook> on_session_ready = std::nullopt)
      -> asio::awaitable<void>;

  auto InvalidateSessions(std::vector<std::string> uris) -> void;

  // Invalidate all sessions - for config/file changes
  // Returns awaitable to ensure sessions are cleared before proceeding
  // Prevents old sessions from holding preamble references during rebuild
  auto InvalidateAllSessions() -> asio::awaitable<void>;

  // Cancel pending session compilation (called when document is closed)
  auto CancelPendingSession(std::string uri) -> void;

  // Schedule cleanup of session after delay (called when document closes)
  // Supports prefetch pattern: if reopened within delay, reuse stored session
  auto ScheduleCleanup(std::string uri) -> void;

  // Updates preamble_manager pointer (thread-safe via strand)
  // Returns awaitable to ensure update completes before proceeding
  // Prevents queueing multiple shared_ptr copies in lambdas
  auto UpdatePreambleManager(
      std::shared_ptr<const PreambleManager> preamble_manager)
      -> asio::awaitable<void>;

  // Callback-based session access - prevents shared_ptr escape
  // Executes callback on session_strand_ with const reference to session
  // Returns std::expected with callback result or error message
  template <typename Fn>
  auto WithSession(std::string uri, Fn callback)
      -> asio::awaitable<std::expected<
          std::invoke_result_t<Fn, const OverlaySession&>, std::string>>;

  // Callback-based compilation state access (Phase 1 - diagnostics)
  // Executes callback on session_strand_ with const reference to compilation
  // state Returns std::expected with callback result or error message
  template <typename Fn>
  auto WithCompilationState(std::string uri, Fn callback)
      -> asio::awaitable<std::expected<
          std::invoke_result_t<Fn, const CompilationState&>, std::string>>;

 private:
  // Pending session creation - concurrent requests share the same events
  struct PendingCreation {
    // Phase 1: Elaboration complete (diagnostics can proceed)
    utils::BroadcastEvent compilation_ready;
    // Phase 2: Indexing complete (symbols/definitions can proceed)
    utils::BroadcastEvent session_ready;
    int version;                         // LSP document version
    std::atomic<bool> cancelled{false};  // Lock-free cancellation flag

    // Optional hooks that execute during session creation (before caching)
    // Useful for server-push features like diagnostics that need guaranteed
    // execution
    std::optional<CompilationReadyHook> on_compilation_ready;
    std::optional<SessionReadyHook> on_session_ready;

    explicit PendingCreation(asio::any_io_executor executor, int doc_version);
  };

  auto StartSessionCreation(
      std::string uri, std::string content, int version,
      std::shared_ptr<const PreambleManager> preamble_manager,
      std::shared_ptr<ProjectLayoutService> layout_service,
      std::optional<CompilationReadyHook> on_compilation_ready,
      std::optional<SessionReadyHook> on_session_ready)
      -> std::shared_ptr<PendingCreation>;

  // Session entry with version and phase tracking
  struct SessionEntry {
    std::shared_ptr<OverlaySession> session;
    int version;
    SessionPhase phase;
  };

  // Dependencies
  asio::any_io_executor executor_;
  std::shared_ptr<spdlog::logger> logger_;

  // Shared state protected by session_strand_
  std::shared_ptr<ProjectLayoutService> layout_service_;
  std::shared_ptr<const PreambleManager> preamble_manager_;
  std::shared_ptr<OpenDocumentTracker> open_tracker_;

  // Strand for thread-safe session map access
  asio::strand<asio::any_io_executor> session_strand_;

  // Type aliases for cleaner member declarations
  using SessionMap = std::unordered_map<std::string, SessionEntry>;
  using PendingMap =
      std::unordered_map<std::string, std::shared_ptr<PendingCreation>>;
  using TimerMap =
      std::unordered_map<std::string, std::unique_ptr<asio::steady_timer>>;

  // Task completion tracking for preamble rebuild coordination
  struct TaskCompletion {
    utils::BroadcastEvent complete;
    explicit TaskCompletion(asio::any_io_executor exec) : complete(exec) {
    }
  };

  // Protected by session_strand_:
  SessionMap sessions_;
  PendingMap pending_;
  TimerMap cleanup_timers_;
  std::vector<std::shared_ptr<TaskCompletion>> active_task_completions_;
  static constexpr auto kCleanupDelay = std::chrono::seconds(5);

  // Background compilation pool (multi-threaded for preamble parsing
  // parallelism)
  std::unique_ptr<asio::thread_pool> compilation_pool_;

  // Strand for serializing overlay elaboration on compilation pool
  // Overlay elaboration triggers lazy operations on shared preamble symbols
  // Serialization prevents concurrent preamble access (Slang is
  // single-threaded)
  asio::strand<asio::any_io_executor> overlay_strand_;
};

// Template method implementations

template <typename Fn>
auto SessionManager::WithSession(std::string uri, Fn callback)
    -> asio::awaitable<std::expected<
        std::invoke_result_t<Fn, const OverlaySession&>, std::string>> {
  // Acquire strand - prevents cleanup during callback execution
  co_await asio::post(session_strand_, asio::use_awaitable);

  // Fast path: Check storage
  if (auto it = sessions_.find(uri); it != sessions_.end()) {
    if (it->second.phase >= SessionPhase::kIndexingComplete) {
      // Execute callback synchronously on strand with const reference
      // Session cannot be removed while we hold strand
      auto result = callback(*it->second.session);
      co_return result;
    }
  }

  // Slow path: Wait for Phase 2 completion
  if (auto it = pending_.find(uri); it != pending_.end()) {
    logger_->debug("Session wait (indexing): {}", uri);

    auto pending = it->second;
    // Release strand during wait
    co_await pending->session_ready.AsyncWait(asio::use_awaitable);
    // Re-acquire strand
    co_await asio::post(session_strand_, asio::use_awaitable);

    // Check if session is now in storage
    if (auto session_it = sessions_.find(uri);
        session_it != sessions_.end() &&
        session_it->second.phase >= SessionPhase::kIndexingComplete) {
      auto result = callback(*session_it->second.session);
      co_return result;
    }

    logger_->info(
        "WithSession: Session not found for {} after notification (cleaned up "
        "or "
        "cancelled)",
        uri);
    co_return std::unexpected(
        "Session not found after notification (cleaned up or cancelled)");
  }

  logger_->info("Session not found: {}", uri);
  co_return std::unexpected("Session not found");
}

template <typename Fn>
auto SessionManager::WithCompilationState(std::string uri, Fn callback)
    -> asio::awaitable<std::expected<
        std::invoke_result_t<Fn, const CompilationState&>, std::string>> {
  // Acquire strand - prevents cleanup during callback execution
  co_await asio::post(session_strand_, asio::use_awaitable);

  // Fast path: Check storage
  if (auto it = sessions_.find(uri); it != sessions_.end()) {
    if (it->second.phase >= SessionPhase::kElaborationComplete) {
      // Create temporary CompilationState on stack
      // Shared_ptrs keep session components alive during callback
      // but don't escape to caller
      CompilationState state{
          .compilation = it->second.session->GetCompilationPtr(),
          .main_buffer_id = it->second.session->GetMainBufferID()};

      // Execute callback with const reference
      auto result = callback(state);
      // Release strand, shared_ptrs in state destroyed, session can be cleaned
      // up
      co_return result;
    }
  }

  // Slow path: Wait for Phase 1 completion
  if (auto it = pending_.find(uri); it != pending_.end()) {
    logger_->debug("Session wait (compilation): {}", uri);

    auto pending = it->second;
    // Release strand during wait
    co_await pending->compilation_ready.AsyncWait(asio::use_awaitable);
    // Re-acquire strand
    co_await asio::post(session_strand_, asio::use_awaitable);

    // Check if session is now in storage
    if (auto session_it = sessions_.find(uri);
        session_it != sessions_.end() &&
        session_it->second.phase >= SessionPhase::kElaborationComplete) {
      CompilationState state{
          .compilation = session_it->second.session->GetCompilationPtr(),
          .main_buffer_id = session_it->second.session->GetMainBufferID()};

      auto result = callback(state);
      co_return result;
    }

    logger_->info(
        "WithCompilationState: Session not found for {} after notification "
        "(cleaned up or cancelled)",
        uri);
    co_return std::unexpected(
        "Session not found after notification (cleaned up or cancelled)");
  }

  logger_->info("Session not found: {}", uri);
  co_return std::unexpected("Session not found");
}

}  // namespace slangd::services

#pragma once

#include <memory>
#include <vector>

#include <asio/any_io_executor.hpp>
#include <asio/async_result.hpp>
#include <asio/post.hpp>
#include <asio/strand.hpp>

namespace slangd::utils {

// Broadcast event primitive for waking multiple waiters simultaneously
//
// Unlike ASIO channels which provide one-shot delivery (first waiter gets
// value, others must check elsewhere), broadcast_event provides true broadcast
// semantics where all waiters are notified simultaneously.
//
// Key features:
// - True broadcast: set() wakes ALL current waiters
// - Late joiners: async_wait() on already-set events completes immediately
// - Thread-safe: Internal strand protects state
// - Lightweight: No data storage, pure notification mechanism
//
// Usage pattern (notification, not data delivery):
//   Producer:                    Consumer:
//   1. Store data in cache       1. co_await event.async_wait()
//   2. event.set()               2. Check cache for data
//
// This design eliminates convoy effects at strand serialization points by
// avoiding large data transfers through the event mechanism itself.
//
// Lifetime safety: Internal state is held via shared_ptr, ensuring lambdas
// posted to the strand can safely access state even if BroadcastEvent is
// destroyed before they execute.
class BroadcastEvent {
 public:
  explicit BroadcastEvent(asio::any_io_executor executor)
      : state_(std::make_shared<State>(executor)) {
  }

  ~BroadcastEvent() = default;

  // Non-copyable, non-movable
  BroadcastEvent(const BroadcastEvent&) = delete;
  auto operator=(const BroadcastEvent&) -> BroadcastEvent& = delete;
  BroadcastEvent(BroadcastEvent&&) = delete;
  auto operator=(BroadcastEvent&&) -> BroadcastEvent& = delete;

  // Wait for the event to be set
  //
  // If already set, completes immediately on executor.
  // If not set, queues handler to be invoked when set() is called.
  //
  // Multiple waiters are supported - all will be notified on set().
  //
  // Technical note: async_initiate is the standard ASIO pattern for custom
  // async operations. It converts completion tokens (like use_awaitable) into
  // handlers (coroutine continuations) that we store and invoke later.
  // The Handler wrapper provides type erasure since each completion token
  // produces a different handler type.
  //
  // Example:
  //   co_await event.AsyncWait(asio::use_awaitable);
  //   // Event has been set, check cache for data
  template <typename CompletionToken>
  auto AsyncWait(CompletionToken&& token) {
    return asio::async_initiate<CompletionToken, void()>(
        [state = state_](auto handler) {
          asio::post(state->strand, [state, h = std::move(handler)]() mutable {
            if (state->ready) {
              // Already signaled, complete immediately
              asio::post(state->executor, std::move(h));
            } else {
              // Not ready yet, queue handler for later notification
              state->waiters.push_back(
                  std::make_unique<ConcreteHandler<decltype(h)>>(std::move(h)));
            }
          });
        },
        std::forward<CompletionToken>(token));
  }

  // Signal the event and wake all waiters
  //
  // All queued handlers will be posted to the executor.
  // Late joiners (calling async_wait after set) will complete immediately.
  //
  // Thread-safe: Can be called from any thread.
  // Idempotent: Multiple calls have no additional effect.
  auto Set() -> void {
    asio::post(state_->strand, [state = state_]() {
      if (state->ready) {
        return;  // Already set, nothing to do
      }

      state->ready = true;

      // Wake all waiters by posting their handlers to executor
      auto waiters = std::move(state->waiters);
      state->waiters.clear();

      for (auto& handler : waiters) {
        asio::post(state->executor, [h = std::move(handler)]() mutable {
          h->Invoke();
        });
      }
    });
  }

  // Check if the event has been set (non-blocking query)
  //
  // Note: For correct async patterns, prefer async_wait() over polling IsSet().
  // This method is primarily useful for testing and diagnostics.
  [[nodiscard]] auto IsSet() const -> bool {
    // Note: This is racy by design - only use for non-critical checks
    return state_->ready;
  }

 private:
  // Move-only function wrapper for ASIO handlers
  struct Handler {
    virtual ~Handler() = default;
    Handler() = default;
    Handler(const Handler&) = delete;
    auto operator=(const Handler&) -> Handler& = delete;
    Handler(Handler&&) = delete;
    auto operator=(Handler&&) -> Handler& = delete;
    virtual auto Invoke() -> void = 0;
  };

  template <typename F>
  struct ConcreteHandler : Handler {
    explicit ConcreteHandler(F&& f) : func(std::move(f)) {
    }
    auto Invoke() -> void override {
      func();
    }
    F func;
  };

  // Internal state held via shared_ptr to ensure lifetime safety
  // Lambdas capture shared_ptr copies, keeping state alive until all
  // pending operations complete
  struct State {
    explicit State(asio::any_io_executor exec)
        : executor(exec), strand(asio::make_strand(exec)) {
    }

    asio::any_io_executor executor;
    asio::strand<asio::any_io_executor> strand;  // Protects ready and waiters
    bool ready = false;
    std::vector<std::unique_ptr<Handler>> waiters;
  };

  std::shared_ptr<State> state_;
};

}  // namespace slangd::utils

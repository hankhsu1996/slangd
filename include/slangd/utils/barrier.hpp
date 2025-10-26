#pragma once

#include <atomic>

#include <asio.hpp>

#include "slangd/utils/broadcast_event.hpp"

namespace slangd::utils {

// Async barrier for coordinating N parallel tasks with 1 waiter
// Workers call Arrive() when done, coordinator calls AsyncWait()
// When all N workers arrive, the waiter is notified
class Barrier {
 public:
  Barrier(asio::any_io_executor executor, size_t count)
      : completed_(0), target_(count), event_(executor) {
  }

  // Called by worker tasks when they complete
  // Last worker to arrive signals the event
  auto Arrive() -> void {
    if (completed_.fetch_add(1, std::memory_order_release) + 1 == target_) {
      event_.Set();
    }
  }

  // Wait for all workers to arrive (async, non-blocking)
  auto AsyncWait(
      asio::completion_token_for<void(std::error_code)> auto&& token) {
    return event_.AsyncWait(std::forward<decltype(token)>(token));
  }

 private:
  std::atomic<size_t> completed_;
  size_t target_;
  BroadcastEvent event_;
};

}  // namespace slangd::utils

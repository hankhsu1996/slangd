#pragma once

#include <optional>

#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/use_awaitable.hpp>

#include "slangd/utils/broadcast_event.hpp"

namespace slangd::utils {

// SharedTask pattern for lifecycle-bound coroutines
//
// Problem: asio::detached causes coroutine frames to persist until io_context
// shutdown, leading to unbounded memory growth when lambdas capture heavy
// resources (e.g., shared_ptr<PreambleManager>).
//
// Solution: Store awaitable in struct, start with lightweight detached
// executor that signals completion via BroadcastEvent.
//
// Usage:
//   auto task = std::make_shared<SharedTask>(
//       co_spawn(executor, [preamble]() { /* work */ }, use_awaitable),
//       executor
//   );
//   task->Start();
//   active_tasks_.push_back(task);
//
//   // Multiple waiters:
//   co_await task->Wait();
//
// How it works:
// - Heavy task (use_awaitable): Captures preamble, destroyed when work
// completes
// - Lightweight executor (detached): Only holds 'this', awaits task and signals
// - Task frame destroyed → preamble released (not leaked!)
struct SharedTask {
  SharedTask(asio::awaitable<void> task, asio::any_io_executor executor)
      : task_(std::move(task)), executor_(executor), done_(executor) {
  }

  ~SharedTask() = default;

  SharedTask(const SharedTask&) = delete;
  SharedTask(SharedTask&&) = delete;
  auto operator=(const SharedTask&) -> SharedTask& = delete;
  auto operator=(SharedTask&&) -> SharedTask& = delete;

  void Start() {
    asio::co_spawn(
        executor_,
        [this]() -> asio::awaitable<void> {
          try {
            co_await std::move(*task_);  // Await the heavy task
            task_.reset();  // Explicitly destroy awaitable and its frame
          } catch (...) {
            // Task failed, still signal completion
            task_.reset();  // Clean up even on failure
          }
          done_.Set();
        },
        asio::detached);
  }

  auto Wait() -> asio::awaitable<void> {
    co_await done_.AsyncWait(asio::use_awaitable);
  }

 private:
  std::optional<asio::awaitable<void>> task_;
  asio::any_io_executor executor_;
  BroadcastEvent done_;
};

}  // namespace slangd::utils

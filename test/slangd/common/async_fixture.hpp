#pragma once

#include <exception>

#include <asio.hpp>
#include <catch2/catch_all.hpp>

namespace slangd::test {

// Reusable async test runner for coroutine-based tests
// Extracted from global_catalog_test.cpp and overlay_session_test.cpp
// to eliminate duplication
template <typename F>
void RunAsyncTest(F&& test_fn) {
  asio::io_context io_context;
  auto executor = io_context.get_executor();

  bool completed = false;
  std::exception_ptr exception;

  asio::co_spawn(
      io_context,
      [fn = std::forward<F>(test_fn), &completed, &exception,
       executor]() -> asio::awaitable<void> {
        try {
          co_await fn(executor);
          completed = true;
        } catch (...) {
          exception = std::current_exception();
          completed = true;
        }
      },
      asio::detached);

  io_context.run();

  if (exception) {
    std::rethrow_exception(exception);
  }

  REQUIRE(completed);
}

}  // namespace slangd::test

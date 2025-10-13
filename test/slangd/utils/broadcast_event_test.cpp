#include "slangd/utils/broadcast_event.hpp"

#include <atomic>
#include <chrono>
#include <memory>

#include <asio.hpp>
#include <catch2/catch_all.hpp>
#include <spdlog/spdlog.h>

#include "test/slangd/common/async_fixture.hpp"

constexpr auto kLogLevel = spdlog::level::debug;

auto main(int argc, char* argv[]) -> int {
  spdlog::set_level(kLogLevel);
  spdlog::set_pattern("[%l] %v");
  return Catch::Session().run(argc, argv);
}

using slangd::test::RunAsyncTest;
using slangd::utils::BroadcastEvent;

TEST_CASE("BroadcastEvent basic set and wait", "[broadcast_event]") {
  RunAsyncTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    BroadcastEvent event(executor);

    // Set the event
    event.Set();

    // Wait should complete immediately since already set
    co_await event.AsyncWait(asio::use_awaitable);

    // Test passes if we reach here
    co_return;
  });
}

TEST_CASE(
    "BroadcastEvent late joiner completes immediately", "[broadcast_event]") {
  RunAsyncTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    BroadcastEvent event(executor);

    // Set before waiting
    event.Set();

    // Multiple late joiners should all complete immediately
    co_await event.AsyncWait(asio::use_awaitable);
    co_await event.AsyncWait(asio::use_awaitable);
    co_await event.AsyncWait(asio::use_awaitable);

    REQUIRE(event.IsSet());
    co_return;
  });
}

TEST_CASE("BroadcastEvent wakes multiple waiters", "[broadcast_event]") {
  RunAsyncTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    auto event = std::make_shared<BroadcastEvent>(executor);
    std::atomic<int> completed_count{0};

    // Spawn 5 waiters
    for (int i = 0; i < 5; ++i) {
      asio::co_spawn(
          executor,
          [event, &completed_count]() -> asio::awaitable<void> {
            co_await event->AsyncWait(asio::use_awaitable);
            completed_count.fetch_add(1, std::memory_order_relaxed);
          },
          asio::detached);
    }

    // Give waiters time to start waiting
    asio::steady_timer timer(executor);
    timer.expires_after(std::chrono::milliseconds(50));
    co_await timer.async_wait(asio::use_awaitable);

    // Set event - should wake all 5 waiters
    event->Set();

    // Give waiters time to complete
    timer.expires_after(std::chrono::milliseconds(50));
    co_await timer.async_wait(asio::use_awaitable);

    // All 5 should have completed
    REQUIRE(completed_count.load() == 5);
    co_return;
  });
}

TEST_CASE("BroadcastEvent mixed early and late joiners", "[broadcast_event]") {
  RunAsyncTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    auto event = std::make_shared<BroadcastEvent>(executor);
    std::atomic<int> early_count{0};
    std::atomic<int> late_count{0};

    // Spawn 3 early waiters (before set)
    for (int i = 0; i < 3; ++i) {
      asio::co_spawn(
          executor,
          [event, &early_count]() -> asio::awaitable<void> {
            co_await event->AsyncWait(asio::use_awaitable);
            early_count.fetch_add(1, std::memory_order_relaxed);
          },
          asio::detached);
    }

    // Give early waiters time to start waiting
    asio::steady_timer timer(executor);
    timer.expires_after(std::chrono::milliseconds(50));
    co_await timer.async_wait(asio::use_awaitable);

    // Set event
    event->Set();

    // Give early waiters time to complete
    timer.expires_after(std::chrono::milliseconds(50));
    co_await timer.async_wait(asio::use_awaitable);

    // Now spawn 2 late joiners (after set)
    for (int i = 0; i < 2; ++i) {
      asio::co_spawn(
          executor,
          [event, &late_count]() -> asio::awaitable<void> {
            co_await event->AsyncWait(asio::use_awaitable);
            late_count.fetch_add(1, std::memory_order_relaxed);
          },
          asio::detached);
    }

    // Give late joiners time to complete
    timer.expires_after(std::chrono::milliseconds(50));
    co_await timer.async_wait(asio::use_awaitable);

    // All should have completed
    REQUIRE(early_count.load() == 3);
    REQUIRE(late_count.load() == 2);
    REQUIRE(event->IsSet());
    co_return;
  });
}

TEST_CASE("BroadcastEvent idempotent set", "[broadcast_event]") {
  RunAsyncTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    auto event = std::make_shared<BroadcastEvent>(executor);
    std::atomic<int> completed_count{0};

    // Spawn 3 waiters
    for (int i = 0; i < 3; ++i) {
      asio::co_spawn(
          executor,
          [event, &completed_count]() -> asio::awaitable<void> {
            co_await event->AsyncWait(asio::use_awaitable);
            completed_count.fetch_add(1, std::memory_order_relaxed);
          },
          asio::detached);
    }

    // Give waiters time to start waiting
    asio::steady_timer timer(executor);
    timer.expires_after(std::chrono::milliseconds(50));
    co_await timer.async_wait(asio::use_awaitable);

    // Call Set multiple times - should be idempotent
    event->Set();
    event->Set();
    event->Set();

    // Give waiters time to complete
    timer.expires_after(std::chrono::milliseconds(50));
    co_await timer.async_wait(asio::use_awaitable);

    // Should complete exactly once per waiter (not 3 times!)
    REQUIRE(completed_count.load() == 3);
    REQUIRE(event->IsSet());
    co_return;
  });
}

TEST_CASE(
    "BroadcastEvent cache-first pattern simulation", "[broadcast_event]") {
  RunAsyncTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    // Simulate the cache-first pattern from the plan
    auto event = std::make_shared<BroadcastEvent>(executor);
    auto cache = std::make_shared<std::atomic<int>>(0);  // Simulated cache
    std::atomic<int> consumer_results{0};

    // Producer: Store in cache, then broadcast
    asio::co_spawn(
        executor,
        [event, cache]() -> asio::awaitable<void> {
          // Simulate compilation delay
          asio::steady_timer timer(co_await asio::this_coro::executor);
          timer.expires_after(std::chrono::milliseconds(100));
          co_await timer.async_wait(asio::use_awaitable);

          // Store result in cache FIRST
          cache->store(42, std::memory_order_release);

          // Then broadcast
          event->Set();
          co_return;
        },
        asio::detached);

    // Consumer 1: Wait for event, then check cache
    asio::co_spawn(
        executor,
        [event, cache, &consumer_results]() -> asio::awaitable<void> {
          co_await event->AsyncWait(asio::use_awaitable);
          int value = cache->load(std::memory_order_acquire);
          consumer_results.fetch_add(value, std::memory_order_relaxed);
          co_return;
        },
        asio::detached);

    // Consumer 2: Wait for event, then check cache
    asio::co_spawn(
        executor,
        [event, cache, &consumer_results]() -> asio::awaitable<void> {
          co_await event->AsyncWait(asio::use_awaitable);
          int value = cache->load(std::memory_order_acquire);
          consumer_results.fetch_add(value, std::memory_order_relaxed);
          co_return;
        },
        asio::detached);

    // Wait for all to complete
    asio::steady_timer timer(executor);
    timer.expires_after(std::chrono::milliseconds(200));
    co_await timer.async_wait(asio::use_awaitable);

    // Both consumers should have gotten the value from cache
    REQUIRE(consumer_results.load() == 84);  // 42 + 42
    co_return;
  });
}

TEST_CASE("BroadcastEvent IsSet reflects state", "[broadcast_event]") {
  RunAsyncTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    BroadcastEvent event(executor);

    // Initially not set
    REQUIRE_FALSE(event.IsSet());

    // After Set, should be set
    event.Set();

    // Give time for Set() to propagate through strand
    asio::steady_timer timer(executor);
    timer.expires_after(std::chrono::milliseconds(50));
    co_await timer.async_wait(asio::use_awaitable);

    REQUIRE(event.IsSet());
    co_return;
  });
}

TEST_CASE("BroadcastEvent stress test with many waiters", "[broadcast_event]") {
  RunAsyncTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    auto event = std::make_shared<BroadcastEvent>(executor);
    std::atomic<int> completed_count{0};

    constexpr int kNumWaiters = 100;

    // Spawn many waiters
    for (int i = 0; i < kNumWaiters; ++i) {
      asio::co_spawn(
          executor,
          [event, &completed_count]() -> asio::awaitable<void> {
            co_await event->AsyncWait(asio::use_awaitable);
            completed_count.fetch_add(1, std::memory_order_relaxed);
          },
          asio::detached);
    }

    // Give waiters time to start waiting
    asio::steady_timer timer(executor);
    timer.expires_after(std::chrono::milliseconds(100));
    co_await timer.async_wait(asio::use_awaitable);

    // Set event - should wake all waiters
    event->Set();

    // Give waiters time to complete
    timer.expires_after(std::chrono::milliseconds(200));
    co_await timer.async_wait(asio::use_awaitable);

    // All should have completed
    REQUIRE(completed_count.load() == kNumWaiters);
    co_return;
  });
}

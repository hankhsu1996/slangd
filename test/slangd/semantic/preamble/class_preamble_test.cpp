#include <cstdlib>
#include <string>

#include <asio.hpp>
#include <catch2/catch_all.hpp>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include "../../common/async_fixture.hpp"
#include "../../common/multifile_semantic_fixture.hpp"

constexpr auto kLogLevel = spdlog::level::debug;

auto main(int argc, char* argv[]) -> int {
  spdlog::set_level(kLogLevel);
  spdlog::set_pattern("[%l] %v");

  setenv("TEST_SHARD_INDEX", "0", 0);
  setenv("TEST_TOTAL_SHARDS", "1", 0);
  setenv("TEST_SHARD_STATUS_FILE", "", 0);

  return Catch::Session().run(argc, argv);
}

using slangd::test::MultiFileSemanticFixture;
using slangd::test::RunAsyncTest;
using Fixture = MultiFileSemanticFixture;

TEST_CASE(
    "Parameterized class static method call with cross-file preamble",
    "[class][preamble][specialization]") {
  RunAsyncTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    Fixture fixture;

    const std::string def = R"(
      package util_pkg;
        parameter TABLE_SIZE = 16;
        parameter OUTPUT_WIDTH = 8;

        virtual class HelperClass#(parameter int INDEX, WIDTH);
          static function automatic logic [WIDTH-1:0] compute(logic [WIDTH-1:0] input_val);
            return input_val;
          endfunction
        endclass
      endpackage
    )";

    const std::string ref = R"(
      module processor;
        import util_pkg::*;
        logic [7:0] result;

        initial begin
          result = HelperClass#(.INDEX(5), .WIDTH(OUTPUT_WIDTH))::compute(8'h42);
        end
      endmodule
    )";

    fixture.CreateBufferIDOffset();
    fixture.CreateFile("util_pkg.sv", def);
    fixture.CreateFile("processor.sv", ref);

    auto session = co_await fixture.BuildSession("processor.sv", executor);
    Fixture::AssertNoErrors(*session);
    Fixture::AssertCrossFileDef(*session, ref, def, "HelperClass", 0, 0);
    Fixture::AssertCrossFileDef(*session, ref, def, "INDEX", 0, 0);
    Fixture::AssertCrossFileDef(*session, ref, def, "WIDTH", 0, 0);
    Fixture::AssertCrossFileDef(*session, ref, def, "OUTPUT_WIDTH", 0, 0);
    Fixture::AssertCrossFileDef(*session, ref, def, "compute", 0, 0);

    co_return;
  });
}

TEST_CASE(
    "Duplicate parameterized class specialization in package and module",
    "[class][preamble][specialization][cache]") {
  RunAsyncTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    Fixture fixture;

    const std::string def = R"(
      package cache_pkg;
        parameter CACHE_LINE_SIZE = 64;

        virtual class Cache#(parameter int SIZE, WIDTH);
          static function automatic logic [WIDTH-1:0] read(logic [WIDTH-1:0] addr);
            return addr;
          endfunction
        endclass

        // Specialization created in PREAMBLE (package)
        typedef Cache#(.SIZE(128), .WIDTH(CACHE_LINE_SIZE)) L1Cache;
      endpackage
    )";

    const std::string ref = R"(
      module cpu;
        import cache_pkg::*;
        logic [63:0] data;

        // DUPLICATE specialization - same parameters as package typedef
        // Tests that Slang's cache key correctly reuses the same specialized class
        typedef Cache#(.SIZE(128), .WIDTH(CACHE_LINE_SIZE)) L1CacheLocal;

        initial begin
          // Method call on preamble specialization
          data = L1Cache::read(64'h1000);
          // Method call on local (duplicate) specialization
          data = L1CacheLocal::read(64'h2000);
        end
      endmodule
    )";

    fixture.CreateBufferIDOffset();
    fixture.CreateFile("cache_pkg.sv", def);
    fixture.CreateFile("cpu.sv", ref);

    auto session = co_await fixture.BuildSession("cpu.sv", executor);
    Fixture::AssertNoErrors(*session);

    // TODO: L1Cache should resolve to typedef (line 11) not generic class (line
    // 4) Currently resolves to generic class Cache definition More intuitive
    // behavior: resolve to the typedef name the user wrote
    // Fixture::AssertCrossFileDef(*session, ref, def, "L1Cache", 0, 0);

    // CRITICAL: Both "read" calls should resolve to the SAME generic class
    // method First "read" call (on L1Cache from package)
    Fixture::AssertCrossFileDef(*session, ref, def, "read", 0, 0);
    // Second "read" call (on L1CacheLocal from module)
    Fixture::AssertCrossFileDef(*session, ref, def, "read", 1, 0);

    // Parameter reference should resolve correctly
    Fixture::AssertCrossFileDef(*session, ref, def, "CACHE_LINE_SIZE", 0, 0);

    co_return;
  });
}

TEST_CASE(
    "Specialized class with virtual methods used as class property",
    "[class][preamble][specialization][virtual][regression]") {
  RunAsyncTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    Fixture fixture;

    // Minimal reproduction of UVM uvm_event pattern that caused invalid
    // coordinates Key pattern: default argument calling a virtual method
    // (data=get_default_data())
    const std::string def = R"(
      package uvm_pkg;
        // Forward declaration to avoid "used before declared" errors in CI
        typedef class uvm_object;

        virtual class uvm_event_base#(parameter type T = int) extends uvm_object;
          T default_data;

          // Virtual method returning T
          virtual function T get_default_data();
            return default_data;
          endfunction

          // CRITICAL: Default argument calls get_default_data()
          // This creates a cross-compilation reference that can cause invalid coordinates
          virtual function void trigger(T data = get_default_data());
            // Trigger implementation
          endfunction

          virtual function void reset(bit wakeup = 0);
            T trigger_data;
            trigger_data = get_default_data();
          endfunction
        endclass

        // Full class body (forward declared above)
        virtual class uvm_object;
        endclass
      endpackage
    )";

    const std::string ref = R"(
      package user_pkg;
        import uvm_pkg::*;

        // Config class with uvm_event property
        class test_config extends uvm_object;
          uvm_event_base#(int) state_event;
        endclass

        // Monitor class with config property
        class test_monitor extends uvm_object;
          test_config cfg;

          function void run_phase();
            // CRITICAL: Nested property access calling trigger() without arguments
            // This pattern: cfg.state_event.trigger() triggers the bug
            cfg.state_event.trigger();
          endfunction
        endclass
      endpackage
    )";

    fixture.CreateBufferIDOffset();
    fixture.CreateFile("uvm_pkg.sv", def);
    fixture.CreateFile("user_pkg.sv", ref);

    // This should not crash or fail with invalid coordinates error
    auto session = co_await fixture.BuildSession("user_pkg.sv", executor);
    Fixture::AssertNoErrors(*session);

    // The critical test: semantic indexing completes without:
    // - "Failed to build semantic index" error
    // - "invalid coordinates" error
    // - Crash/segfault
    // Cross-file navigation for specialized classes is best-effort

    co_return;
  });
}

TEST_CASE(
    "Inherited class property from parameterized base class",
    "[class][preamble][inheritance][property][regression]") {
  RunAsyncTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    Fixture fixture;

    // Minimal reproduction of UVM uvm_driver.seq_item_port pattern
    // Key pattern: accessing inherited property from parameterized base class
    const std::string def = R"(
      package driver_pkg;
        class seq_item_port#(type T = int);
          function void get_next_item(ref T item);
          endfunction
        endclass

        virtual class base_driver#(type REQ = int);
          seq_item_port#(REQ) seq_item_port;

          function new();
          endfunction
        endclass
      endpackage
    )";

    const std::string ref = R"(
      package test_pkg;
        import driver_pkg::*;

        class my_seq_item;
        endclass

        class my_driver extends base_driver#(my_seq_item);
          function void run();
            my_seq_item req;
            // CRITICAL: Accessing inherited property seq_item_port
            // This causes invalid coordinates because:
            // 1. my_driver extends base_driver#(my_seq_item) - specialization in overlay
            // 2. seq_item_port is defined in generic base_driver in preamble
            // 3. Symbol location points to preamble but compilation is overlay
            seq_item_port.get_next_item(req);
          endfunction
        endclass
      endpackage
    )";

    fixture.CreateBufferIDOffset();
    fixture.CreateFile("driver_pkg.sv", def);
    fixture.CreateFile("test_pkg.sv", ref);

    // This should not crash or fail with invalid coordinates error
    auto session = co_await fixture.BuildSession("test_pkg.sv", executor);
    Fixture::AssertNoErrors(*session);

    // The critical test: semantic indexing completes without:
    // - "Failed to build semantic index" error
    // - "invalid coordinates" error for seq_item_port reference
    // - Crash/segfault

    co_return;
  });
}

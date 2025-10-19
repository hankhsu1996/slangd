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
    "Cross-compilation package binding with PreambleManager",
    "[package][preamble]") {
  RunAsyncTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    Fixture fixture;

    const std::string def = R"(
      package config_pkg;
        parameter DATA_WIDTH = 32;
        parameter ADDR_WIDTH = 16;
        typedef logic [DATA_WIDTH-1:0] word_t;
        typedef logic [ADDR_WIDTH-1:0] addr_t;
      endpackage
    )";

    const std::string ref = R"(
      module processor;
        import config_pkg::*;
        word_t instruction_reg;
        addr_t program_counter;
        parameter WIDTH = DATA_WIDTH;
      endmodule
    )";

    fixture.CreateBufferIDOffset();
    fixture.CreateFile("package_file.sv", def);
    fixture.CreateFile("module_file.sv", ref);

    auto session = fixture.BuildSession("module_file.sv", executor);
    REQUIRE(session != nullptr);

    Fixture::AssertCrossFileDef(*session, ref, def, "config_pkg", 0, 0);
    Fixture::AssertCrossFileDef(*session, ref, def, "DATA_WIDTH", 0, 0);
    Fixture::AssertCrossFileDef(*session, ref, def, "word_t", 0, 0);
    Fixture::AssertCrossFileDef(*session, ref, def, "addr_t", 0, 0);

    co_return;
  });
}

TEST_CASE(
    "Multiple package imports with cross-compilation binding",
    "[package][preamble]") {
  RunAsyncTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    Fixture fixture;

    const std::string def1 = R"(
      package types_pkg;
        typedef logic [31:0] word_t;
      endpackage
    )";

    const std::string def2 = R"(
      package constants_pkg;
        parameter BUS_WIDTH = 64;
      endpackage
    )";

    const std::string ref = R"(
      module top;
        import types_pkg::*;
        import constants_pkg::*;
        word_t data_reg;
        logic [BUS_WIDTH-1:0] bus;
      endmodule
    )";

    fixture.CreateBufferIDOffset();
    fixture.CreateFile("types_pkg.sv", def1);
    fixture.CreateFile("constants_pkg.sv", def2);
    fixture.CreateFile("top.sv", ref);

    auto session = fixture.BuildSession("top.sv", executor);
    REQUIRE(session != nullptr);

    Fixture::AssertCrossFileDef(*session, ref, def1, "word_t", 0, 0);
    Fixture::AssertCrossFileDef(*session, ref, def2, "BUS_WIDTH", 0, 0);

    co_return;
  });
}

TEST_CASE(
    "Scoped package references with go-to-definition",
    "[package][preamble][scoped]") {
  RunAsyncTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    Fixture fixture;

    const std::string def = R"(
      package config_pkg;
        parameter MAX_COUNT = 255;
        parameter MIN_COUNT = 0;
        typedef logic [7:0] counter_t;
        function automatic logic [7:0] clamp(logic [7:0] val);
          return val;
        endfunction
      endpackage
    )";

    const std::string ref = R"(
      module counter;
        config_pkg::counter_t count;
        logic [7:0] max_val = config_pkg::MAX_COUNT;
        logic [7:0] min_val = config_pkg::MIN_COUNT;
        logic [7:0] clamped = config_pkg::clamp(count);
      endmodule
    )";

    fixture.CreateBufferIDOffset();
    fixture.CreateFile("config_pkg.sv", def);
    fixture.CreateFile("counter.sv", ref);

    auto session = fixture.BuildSession("counter.sv", executor);
    REQUIRE(session != nullptr);

    Fixture::AssertCrossFileDef(*session, ref, def, "config_pkg", 0, 0);
    Fixture::AssertCrossFileDef(*session, ref, def, "counter_t", 0, 0);
    Fixture::AssertCrossFileDef(*session, ref, def, "MAX_COUNT", 0, 0);
    Fixture::AssertCrossFileDef(*session, ref, def, "MIN_COUNT", 0, 0);
    Fixture::AssertCrossFileDef(*session, ref, def, "clamp", 0, 0);

    co_return;
  });
}

TEST_CASE(
    "Struct field go-to-definition with cross-file preamble",
    "[package][preamble][struct]") {
  RunAsyncTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    Fixture fixture;

    const std::string def = R"(
      package types_pkg;
        typedef struct {
          logic [7:0] field_a;
          logic [3:0] field_b;
        } my_struct_t;
      endpackage
    )";

    const std::string ref = R"(
      module test;
        import types_pkg::*;
        my_struct_t s1, s2;

        initial begin
          s2 = '{field_a: s1.field_a, field_b: s1.field_b};
        end
      endmodule
    )";

    fixture.CreateBufferIDOffset();
    fixture.CreateFile("types_pkg.sv", def);
    fixture.CreateFile("test.sv", ref);

    auto session = fixture.BuildSession("test.sv", executor);
    REQUIRE(session != nullptr);

    Fixture::AssertCrossFileDef(*session, ref, def, "field_a", 0, 0);
    Fixture::AssertCrossFileDef(*session, ref, def, "field_b", 0, 0);

    co_return;
  });
}

TEST_CASE(
    "Enum member go-to-definition with cross-file preamble",
    "[package][preamble][enum]") {
  RunAsyncTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    Fixture fixture;

    const std::string def = R"(
      package status_pkg;
        typedef enum logic {
          STATUS_ERROR = 1'b1,
          STATUS_OK    = 1'b0
        } status_t;
      endpackage
    )";

    const std::string ref = R"(
      module processor;
        import status_pkg::*;
        status_t result;

        initial begin
          result = STATUS_OK;
          if (result == STATUS_ERROR) begin
            result = STATUS_OK;
          end
        end
      endmodule
    )";

    fixture.CreateBufferIDOffset();
    fixture.CreateFile("status_pkg.sv", def);
    fixture.CreateFile("processor.sv", ref);

    auto session = fixture.BuildSession("processor.sv", executor);
    REQUIRE(session != nullptr);

    Fixture::AssertCrossFileDef(*session, ref, def, "STATUS_OK", 0, 0);
    Fixture::AssertCrossFileDef(*session, ref, def, "STATUS_ERROR", 0, 0);

    co_return;
  });
}

TEST_CASE(
    "Parameterized class static method call with cross-file preamble",
    "[package][preamble][class]") {
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

    auto session = fixture.BuildSession("processor.sv", executor);
    REQUIRE(session != nullptr);

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
    "[package][preamble][class][specialization][cache]") {
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

    auto session = fixture.BuildSession("cpu.sv", executor);
    REQUIRE(session != nullptr);

    // TODO: L1Cache should resolve to typedef (line 10) not generic class (line
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

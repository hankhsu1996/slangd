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
    "Stress test with many preamble packages to detect BufferID mismatch bugs",
    "[package][preamble][stress]") {
  RunAsyncTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    Fixture fixture;

    // Create 60 preamble packages to ensure BufferID mismatch
    // This simulates real-world scenarios with many packages (like 120
    // packages) When IsInCurrentFile doesn't check IsPreambleSymbol(), it will
    // try to convert preamble symbol locations using overlay's SourceManager,
    // causing BufferID out-of-bounds access → segfault
    for (int i = 0; i < 60; i++) {
      std::string pkg_content = fmt::format(
          R"(
        package pkg_{};
          parameter VALUE_{} = {};
        endpackage
      )",
          i, i, i * 10);

      fixture.CreateFile(fmt::format("pkg_{}.sv", i), pkg_content);
    }

    // Overlay file that imports from first package
    const std::string ref = R"(
      module test;
        import pkg_0::*;
        logic [VALUE_0-1:0] data;
      endmodule
    )";

    fixture.CreateFile("test.sv", ref);

    // This will trigger the bug if IsInCurrentFile doesn't check preamble
    // because compilation.getPackages() will iterate through all 60 preamble
    // packages and try to check if they're in current file using wrong
    // SourceManager
    auto session = fixture.BuildSession("test.sv", executor);
    REQUIRE(session != nullptr);

    // Just verify session was created without crashing
    // The real test is that it doesn't segfault during BuildSession

    co_return;
  });
}

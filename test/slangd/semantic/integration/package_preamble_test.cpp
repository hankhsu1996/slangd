#include <cstdlib>
#include <string>

#include <asio.hpp>
#include <catch2/catch_all.hpp>
#include <spdlog/spdlog.h>

#include "../../common/async_fixture.hpp"
#include "../test_fixtures.hpp"

constexpr auto kLogLevel = spdlog::level::debug;

auto main(int argc, char* argv[]) -> int {
  spdlog::set_level(kLogLevel);
  spdlog::set_pattern("[%l] %v");

  setenv("TEST_SHARD_INDEX", "0", 0);
  setenv("TEST_TOTAL_SHARDS", "1", 0);
  setenv("TEST_SHARD_STATUS_FILE", "", 0);

  return Catch::Session().run(argc, argv);
}

using slangd::semantic::test::MultiFileSemanticFixture;
using slangd::test::RunAsyncTest;
using Fixture = MultiFileSemanticFixture;

// This test verifies that cross-compilation package binding works:
// - Package defined in separate preamble compilation
// - Module imports from package via cross-compilation symbol binding
// - Go-to-definition resolves to preamble package symbols
TEST_CASE(
    "Cross-compilation package binding with PreambleManager",
    "[package][preamble][cross-compilation]") {
  RunAsyncTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    Fixture fixture;

    // Package file loaded by PreambleManager
    const std::string package_content = R"(
      package config_pkg;
        parameter DATA_WIDTH = 32;
        parameter ADDR_WIDTH = 16;
        typedef logic [DATA_WIDTH-1:0] word_t;
        typedef logic [ADDR_WIDTH-1:0] addr_t;
      endpackage
    )";

    // Module file imports from package (cross-compilation binding)
    const std::string module_content = R"(
      module processor;
        import config_pkg::*;
        word_t instruction_reg;
        addr_t program_counter;
        parameter WIDTH = DATA_WIDTH;
      endmodule
    )";

    // Create files on disk - PreambleManager will load package_file.sv
    fixture.CreateFile("package_file.sv", package_content);
    fixture.CreateFile("module_file.sv", module_content);

    // Build session with PreambleManager
    // This should use PreambleAwareCompilation with cross-compilation binding
    auto result = fixture.BuildSessionWithPreamble(
        "module_file.sv", executor);
    REQUIRE(result.session != nullptr);
    REQUIRE(result.preamble_manager != nullptr);

    // Verify PreambleManager has the package
    const auto* pkg = result.preamble_manager->GetPackage("config_pkg");
    REQUIRE(pkg != nullptr);
    REQUIRE(pkg->name == "config_pkg");

    // TODO Phase 3: Once location conversion is implemented, verify:
    // 1. Go-to-definition on "word_t" jumps to package_file.sv
    // 2. Go-to-definition on "addr_t" jumps to package_file.sv
    // 3. Go-to-definition on "DATA_WIDTH" jumps to package_file.sv
    // 4. Go-to-definition on "config_pkg" in import statement works

    // For now, just verify the session was created successfully
    // and compilation didn't crash (proves cross-compilation binding works)
    REQUIRE(result.session->GetSemanticIndex().GetSemanticEntries().size() > 0);

    co_return;
  });
}

// Test wildcard package import with PreambleManager
TEST_CASE(
    "Wildcard package import with cross-compilation binding",
    "[package][preamble][cross-compilation]") {
  RunAsyncTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    Fixture fixture;

    const std::string package_content = R"(
      package math_pkg;
        parameter MAX_VALUE = 100;
        parameter MIN_VALUE = 0;
        typedef logic [7:0] byte_t;
      endpackage
    )";

    const std::string module_content = R"(
      module calculator;
        import math_pkg::*;
        byte_t result;
        logic [7:0] max = MAX_VALUE;
        logic [7:0] min = MIN_VALUE;
      endmodule
    )";

    fixture.CreateFile("math_pkg.sv", package_content);
    fixture.CreateFile("calculator.sv", module_content);

    auto result = fixture.BuildSessionWithPreamble(
        "calculator.sv", executor);
    REQUIRE(result.session != nullptr);

    // Verify package exists in preamble
    const auto* pkg = result.preamble_manager->GetPackage("math_pkg");
    REQUIRE(pkg != nullptr);

    // Verify compilation succeeded (wildcard import resolved)
    REQUIRE(result.session->GetSemanticIndex().GetSemanticEntries().size() > 0);

    co_return;
  });
}

// Test multiple package imports with PreambleManager
TEST_CASE(
    "Multiple package imports with cross-compilation binding",
    "[package][preamble][cross-compilation]") {
  RunAsyncTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    Fixture fixture;

    const std::string pkg1_content = R"(
      package types_pkg;
        typedef logic [31:0] word_t;
      endpackage
    )";

    const std::string pkg2_content = R"(
      package constants_pkg;
        parameter BUS_WIDTH = 64;
      endpackage
    )";

    const std::string module_content = R"(
      module top;
        import types_pkg::*;
        import constants_pkg::*;
        word_t data_reg;
        logic [BUS_WIDTH-1:0] bus;
      endmodule
    )";

    fixture.CreateFile("types_pkg.sv", pkg1_content);
    fixture.CreateFile("constants_pkg.sv", pkg2_content);
    fixture.CreateFile("top.sv", module_content);

    auto result =
        fixture.BuildSessionWithPreamble("top.sv", executor);
    REQUIRE(result.session != nullptr);

    // Verify both packages exist in preamble
    const auto* pkg1 = result.preamble_manager->GetPackage("types_pkg");
    const auto* pkg2 = result.preamble_manager->GetPackage("constants_pkg");
    REQUIRE(pkg1 != nullptr);
    REQUIRE(pkg2 != nullptr);

    // Verify compilation succeeded
    REQUIRE(result.session->GetSemanticIndex().GetSemanticEntries().size() > 0);

    co_return;
  });
}

#include <cstdlib>
#include <string>

#include <catch2/catch_all.hpp>
#include <spdlog/spdlog.h>

#include "../common/simple_fixture.hpp"

constexpr auto kLogLevel = spdlog::level::debug;

auto main(int argc, char* argv[]) -> int {
  spdlog::set_level(kLogLevel);
  spdlog::set_pattern("[%l] %v");

  // Suppress Bazel test sharding warnings
  setenv("TEST_SHARD_INDEX", "0", 0);
  setenv("TEST_TOTAL_SHARDS", "1", 0);
  setenv("TEST_SHARD_STATUS_FILE", "", 0);

  return Catch::Session().run(argc, argv);
}

using slangd::test::SimpleTestFixture;
using Fixture = SimpleTestFixture;

TEST_CASE("Valid code has no errors", "[diagnostics]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module test_module;
      logic signal;
    endmodule
  )";

  auto diags = fixture.CompileSourceAndGetDiagnostics(code);
  Fixture::AssertNoErrors(diags);
}

TEST_CASE("Detects syntax errors", "[diagnostics]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module test_module;
      logic signal  // Missing semicolon
      logic another_signal;
    endmodule
  )";

  auto diags = fixture.CompileSourceAndGetDiagnostics(code);
  Fixture::AssertError(diags, "expected ';'");
}

TEST_CASE("Detects semantic errors", "[diagnostics]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module test_module;
      logic [7:0] data;

      initial begin
        undefined_variable = 8'h42;  // Undefined variable
      end
    endmodule
  )";

  auto diags = fixture.CompileSourceAndGetDiagnostics(code);
  Fixture::AssertError(diags, "undefined_variable");
}

TEST_CASE("Handles malformed module", "[diagnostics]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module test_module  // Missing semicolon and endmodule
      logic signal;
  )";

  auto diags = fixture.CompileSourceAndGetDiagnostics(code);
  Fixture::AssertError(diags, "expected");
}

TEST_CASE("Continuous assignment error detection", "[diagnostics]") {
  SimpleTestFixture fixture;

  SECTION("RHS undefined - error correctly reported") {
    std::string code = R"(
      module test_module;
        logic valid_target;
        assign valid_target = undefined_source;
      endmodule
    )";
    auto diags = fixture.CompileSourceAndGetDiagnostics(code);
    Fixture::AssertError(diags, "undefined_source");
  }

  SECTION("Both sides undefined - error correctly reported") {
    std::string code = R"(
      module test_module;
        assign undefined_target = undefined_source;
      endmodule
    )";
    auto diags = fixture.CompileSourceAndGetDiagnostics(code);
    Fixture::AssertError(diags, "undefined_source");
  }

  SECTION(
      "LHS undefined - error correctly reported with default_nettype none") {
    std::string code = R"(
      module test_module;
        logic valid_signal;
        assign undefined_target = valid_signal;
      endmodule
    )";
    auto diags = fixture.CompileSourceAndGetDiagnostics(code);
    Fixture::AssertError(diags, "undefined_target");
  }
}

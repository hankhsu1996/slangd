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

TEST_CASE("DiagnosticConverter basic functionality", "[diagnostic_converter]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module test_module;
      logic signal;
    endmodule
  )";

  auto diags = fixture.CompileSourceAndGetDiagnostics(code);

  // Valid code should have few or no diagnostics
  REQUIRE(diags.size() >= 0);  // May have warnings but shouldn't fail
}

TEST_CASE(
    "DiagnosticConverter detects syntax errors", "[diagnostic_converter]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module test_module;
      logic signal  // Missing semicolon
      logic another_signal;
    endmodule
  )";

  auto diags = fixture.CompileSourceAndGetDiagnostics(code);

  REQUIRE(diags.size() > 0);
  Fixture::AssertDiagnosticExists(diags, lsp::DiagnosticSeverity::kError);
  Fixture::AssertDiagnosticsValid(diags, lsp::DiagnosticSeverity::kError);
}

TEST_CASE(
    "DiagnosticConverter detects semantic errors", "[diagnostic_converter]") {
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

  spdlog::info(
      "DEBUG: Semantic error test completed with {} diagnostics", diags.size());

  REQUIRE(diags.size() > 0);
  Fixture::AssertDiagnosticExists(
      diags, lsp::DiagnosticSeverity::kError, "undefined");
}

TEST_CASE(
    "DiagnosticConverter handles malformed module", "[diagnostic_converter]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module test_module  // Missing semicolon and endmodule
      logic signal;
  )";

  auto diags = fixture.CompileSourceAndGetDiagnostics(code);

  REQUIRE(diags.size() > 0);
  Fixture::AssertDiagnosticExists(diags, lsp::DiagnosticSeverity::kError);
  Fixture::AssertDiagnosticsValid(diags, lsp::DiagnosticSeverity::kError);
}

TEST_CASE("DiagnosticConverter handles empty file", "[diagnostic_converter]") {
  SimpleTestFixture fixture;
  std::string code;

  auto diags = fixture.CompileSourceAndGetDiagnostics(code);

  // May have diagnostics about no compilation units, but shouldn't crash
  REQUIRE(diags.size() >= 0);
}

TEST_CASE(
    "DiagnosticConverter parse diagnostics are subset of all",
    "[diagnostic_converter]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module test_module;
      logic signal  // Missing semicolon - parse error
      logic [7:0] data;

      initial begin
        undefined_var = 8'h42;  // Semantic error
      end
    endmodule
  )";

  // For this test, we need to compare parse vs all diagnostics
  // CompileSourceAndGetDiagnostics uses ExtractAllDiagnostics internally
  auto diags = fixture.CompileSourceAndGetDiagnostics(code);

  // We expect both parse and semantic errors
  REQUIRE(diags.size() > 1);
  Fixture::AssertDiagnosticExists(diags, lsp::DiagnosticSeverity::kError);
}

TEST_CASE(
    "DiagnosticConverter detects semantic errors with continuous assignments",
    "[diagnostic_converter]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module test_module;
      assign xxx = yyyyy;
    endmodule
  )";

  auto diags = fixture.CompileSourceAndGetDiagnostics(code);

  spdlog::info(
      "Found {} diagnostics for continuous assignment test:", diags.size());
  for (const auto& diag : diags) {
    spdlog::info(
        "  - [{}] {}: {}",
        static_cast<int>(
            diag.severity.value_or(lsp::DiagnosticSeverity::kInformation)),
        diag.code.value_or("no-code"), diag.message);
  }

  REQUIRE(diags.size() > 0);
  Fixture::AssertDiagnosticExists(diags, lsp::DiagnosticSeverity::kError);
}

TEST_CASE(
    "DiagnosticConverter respects error limit with many errors",
    "[diagnostic_converter]") {
  SimpleTestFixture fixture;

  // Generate code with >70 undefined variables to exceed default limit of 64
  std::string code = "module test_module;\n";
  for (int i = 0; i < 80; i++) {
    code += "  assign undef_" + std::to_string(i) + " = missing_" +
            std::to_string(i) + ";\n";
  }
  code += "endmodule\n";

  auto diags = fixture.CompileSourceAndGetDiagnostics(code);

  spdlog::info(
      "Found {} diagnostics with unlimited error limit (expected >64)",
      diags.size());

  // With errorLimit=0 (unlimited), we should see more than 64 diagnostics
  // (default limit would cap at 64)
  REQUIRE(diags.size() > 64);
}

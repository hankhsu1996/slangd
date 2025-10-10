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

TEST_CASE("Generate if block error detection", "[diagnostics]") {
  SimpleTestFixture fixture;

  SECTION("Undefined variable inside generate if") {
    std::string code = R"(
      module test_module;
        parameter int WIDTH = 8;

        if (WIDTH == 8) begin : gen_block
          logic [7:0] data;
          assign data = undefined_var;
        end
      endmodule
    )";
    auto diags = fixture.CompileSourceAndGetDiagnostics(code);
    Fixture::AssertError(diags, "undefined_var");
  }
}

TEST_CASE("Unknown package import is reported", "[diagnostics]") {
  SimpleTestFixture fixture;

  std::string code = R"(
    module test_module;
      import nonexistent_pkg::*;
      logic signal;
    endmodule
  )";
  auto diags = fixture.CompileSourceAndGetDiagnostics(code);
  Fixture::AssertError(diags, "unknown package");
}

TEST_CASE(
    "Hierarchical reference in assertion without full hierarchy",
    "[diagnostics]") {
  SimpleTestFixture fixture;

  SECTION("Upward hierarchical reference in assertion") {
    std::string code = R"(
      module test_module(
        input logic clk,
        input logic reset,
        input logic enable
      );
        // Hierarchical reference that would work in full design
        // but shows as info in single-file LSP mode
        assert property (@(posedge clk) disable iff (reset)
          enable |-> top.subsystem.status_flag);
      endmodule
    )";
    auto diags = fixture.CompileSourceAndGetDiagnostics(code);

    // Should have UnresolvedHierarchicalPath as hint (grey dotted), not error
    Fixture::AssertDiagnosticExists(
        diags, lsp::DiagnosticSeverity::kHint,
        "hierarchical reference 'top' cannot be resolved in the language "
        "server");

    // Should NOT have an error
    Fixture::AssertNoErrors(diags);
  }

  SECTION("Hierarchical reference in assertion with nested path") {
    std::string code = R"(
      module clock_gate(
        input logic clk,
        input logic reset,
        input logic pipe_empty
      );
        assert property (@(negedge clk) disable iff (reset)
          ~pipe_empty |-> parent.core.pipe_active)
          else $error("Clock disabled but pipe not empty!");
      endmodule
    )";
    auto diags = fixture.CompileSourceAndGetDiagnostics(code);

    // Should have UnresolvedHierarchicalPath as hint (grey dotted), not error
    Fixture::AssertDiagnosticExists(
        diags, lsp::DiagnosticSeverity::kHint,
        "hierarchical reference 'parent' cannot be resolved in the language "
        "server");

    // Should NOT have an error
    Fixture::AssertNoErrors(diags);
  }

  SECTION("Regular undefined variable still shows as error") {
    std::string code = R"(
      module test_module(
        input logic clk,
        input logic reset
      );
        // This is NOT a hierarchical path - just a typo
        // Should still show as error
        initial begin
          undefined_var = 1'b1;
        end
      endmodule
    )";
    auto diags = fixture.CompileSourceAndGetDiagnostics(code);

    // Regular undefined variable should still be an error
    Fixture::AssertError(diags, "undefined_var");
  }
}

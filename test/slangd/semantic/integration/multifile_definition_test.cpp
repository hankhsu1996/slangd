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

TEST_CASE("Definition lookup for package imports", "[definition][multifile]") {
  Fixture fixture;

  const std::string package_content = R"(
    package test_pkg;
      parameter WIDTH = 32;
      typedef logic [WIDTH-1:0] data_t;
    endpackage
  )";

  const std::string module_content = R"(
    module test_module;
      import test_pkg::*;
      data_t my_data;
    endmodule
  )";

  auto result = fixture.CreateBuilder()
                    .SetCurrentFile(module_content, "test_module")
                    .AddUnopendFile(package_content, "test_pkg")
                    .Build();
  REQUIRE(result.index != nullptr);

  fixture.AssertCrossFileDefinition(*result.index, module_content, "data_t");
}

TEST_CASE(
    "Definition lookup for package name in import statement",
    "[definition][multifile]") {
  Fixture fixture;

  const std::string package_content = R"(
    package my_pkg;
      parameter WIDTH = 32;
      typedef logic [WIDTH-1:0] data_t;
    endpackage
  )";

  const std::string module_content = R"(
    module test_module;
      import my_pkg::*;
      data_t my_data;
    endmodule
  )";

  auto result = fixture.CreateBuilder()
                    .SetCurrentFile(module_content, "test_module")
                    .AddUnopendFile(package_content, "my_pkg")
                    .Build();
  REQUIRE(result.index != nullptr);

  fixture.AssertCrossFileDefinition(*result.index, module_content, "my_pkg");
}

TEST_CASE(
    "Definition lookup for cross-file module instantiation",
    "[definition][multifile]") {
  RunAsyncTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    Fixture fixture;

    const std::string alu_content = R"(
      module ALU #(parameter WIDTH = 8) (
        input logic [WIDTH-1:0] a, b,
        output logic [WIDTH-1:0] result
      );
      endmodule
    )";

    const std::string top_content = R"(
      module top;
        logic [7:0] x, y, z;
        ALU #(.WIDTH(8)) alu_inst (.a(x), .b(y), .result(z));
      endmodule
    )";

    fixture.CreateFile("alu.sv", alu_content);
    fixture.CreateFile("top.sv", top_content);

    auto result = fixture.BuildSessionFromDiskWithCatalog("top.sv", executor);
    REQUIRE(result.session != nullptr);
    REQUIRE(result.catalog != nullptr);

    Fixture::AssertCrossFileDefinition(result, "ALU", "top.sv", "alu.sv");

    co_return;
  });
}

TEST_CASE(
    "Definition lookup for same-file module instantiation",
    "[definition][multifile]") {
  Fixture fixture;

  const std::string content = R"(
    module counter;
    endmodule

    module top;
      counter cnt_inst;
    endmodule
  )";

  auto result =
      fixture.CreateBuilder().SetCurrentFile(content, "single_file").Build();
  REQUIRE(result.index != nullptr);

  fixture.AssertSameFileDefinition(*result.index, content, "counter");
}

TEST_CASE(
    "Definition lookup for unknown module doesn't crash",
    "[definition][multifile]") {
  Fixture fixture;

  const std::string content = R"(
    module top;
      UnknownModule inst;
    endmodule
  )";

  auto result = fixture.CreateBuilder().SetCurrentFile(content, "test").Build();
  REQUIRE(result.index != nullptr);

  fixture.AssertDefinitionNotCrash(*result.index, content, "UnknownModule");
}

TEST_CASE(
    "Port navigation - cross-file with edge cases",
    "[definition][multifile][port]") {
  RunAsyncTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    Fixture fixture;

    const std::string def = R"(
      module adder (
        input logic a_port, b_port, c_port,
        output logic sum_port
      );
      endmodule
    )";

    const std::string ref = R"(
      module top;
        logic x, y, z, result;
        adder inst (.a_port(x), y, .c_port(z), result);
        adder inst2 (.a_port(x), .nonexistent(y), .sum_port(result));
      endmodule
    )";

    fixture.CreateFile("adder.sv", def);
    fixture.CreateFile("top.sv", ref);

    auto result = fixture.BuildSessionFromDiskWithCatalog("top.sv", executor);
    REQUIRE(result.session != nullptr);
    REQUIRE(result.catalog != nullptr);

    Fixture::AssertCrossFileDef(result, ref, def, "a_port", 0, 0);
    Fixture::AssertCrossFileDef(result, ref, def, "c_port", 0, 0);
    Fixture::AssertCrossFileDef(result, ref, def, "sum_port", 0, 0);

    auto location =
        Fixture::FindLocationInSession(*result.session, "nonexistent");
    auto def_loc =
        result.session->GetSemanticIndex().LookupDefinitionAt(location);

    co_return;
  });
}

// NOTE: Same-file port/parameter navigation not yet implemented
// Same-file instantiations create InstanceSymbol (not UninstantiatedDefSymbol)
// Future work: Add handler for InstanceSymbol to support same-file cases

TEST_CASE(
    "Parameter navigation - cross-file with edge cases",
    "[definition][multifile][parameter]") {
  RunAsyncTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    Fixture fixture;

    const std::string def = R"(
      module configurable #(
        parameter PARAM_A = 1,
        parameter PARAM_B = 2,
        parameter PARAM_C = 3
      ) (input logic clk);
      endmodule
    )";

    const std::string ref = R"(
      module top;
        logic clk;
        configurable #(.PARAM_A(10), 20, .PARAM_C(30)) inst1 (.clk(clk));
        configurable #(.PARAM_A(5), .INVALID(99)) inst2 (.clk(clk));
      endmodule
    )";

    fixture.CreateFile("configurable.sv", def);
    fixture.CreateFile("top.sv", ref);

    auto result = fixture.BuildSessionFromDiskWithCatalog("top.sv", executor);
    REQUIRE(result.session != nullptr);
    REQUIRE(result.catalog != nullptr);

    Fixture::AssertCrossFileDef(result, ref, def, "PARAM_A", 0, 0);
    Fixture::AssertCrossFileDef(result, ref, def, "PARAM_C", 0, 0);

    auto location = Fixture::FindLocationInSession(*result.session, "INVALID");
    auto def_loc =
        result.session->GetSemanticIndex().LookupDefinitionAt(location);

    co_return;
  });
}

TEST_CASE(
    "Complete navigation - module, ports, and parameters",
    "[definition][multifile][integration]") {
  RunAsyncTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    Fixture fixture;

    const std::string def = R"(
      module ALU #(
        parameter DATA_WIDTH = 8,
        parameter OP_WIDTH = 4
      ) (
        input logic [DATA_WIDTH-1:0] operand_a, operand_b,
        input logic [OP_WIDTH-1:0] operation,
        output logic [DATA_WIDTH-1:0] result
      );
      endmodule
    )";

    const std::string ref = R"(
      module top;
        logic [31:0] a, b, res;
        logic [3:0] op;
        ALU #(.DATA_WIDTH(32), .OP_WIDTH(4)) alu_inst (
          .operand_a(a),
          .operand_b(b),
          .operation(op),
          .result(res)
        );
      endmodule
    )";

    fixture.CreateFile("alu.sv", def);
    fixture.CreateFile("top.sv", ref);

    auto result = fixture.BuildSessionFromDiskWithCatalog("top.sv", executor);
    REQUIRE(result.session != nullptr);
    REQUIRE(result.catalog != nullptr);

    Fixture::AssertCrossFileDefinition(result, "ALU", "top.sv", "alu.sv");

    Fixture::AssertCrossFileDef(result, ref, def, "DATA_WIDTH", 0, 0);
    Fixture::AssertCrossFileDef(result, ref, def, "OP_WIDTH", 0, 0);

    Fixture::AssertCrossFileDef(result, ref, def, "operand_a", 0, 0);
    Fixture::AssertCrossFileDef(result, ref, def, "operand_b", 0, 0);
    Fixture::AssertCrossFileDef(result, ref, def, "operation", 0, 0);
    Fixture::AssertCrossFileDef(result, ref, def, "result", 0, 0);

    co_return;
  });
}

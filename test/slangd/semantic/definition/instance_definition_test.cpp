#include <cstdlib>
#include <memory>
#include <string>

#include <catch2/catch_all.hpp>
#include <slang/ast/Symbol.h>
#include <slang/util/Enum.h>
#include <spdlog/spdlog.h>

#include "../../common/simple_fixture.hpp"

constexpr auto kLogLevel = spdlog::level::debug;

auto main(int argc, char* argv[]) -> int {
  spdlog::set_level(kLogLevel);
  spdlog::set_pattern("[%l] %v");

  setenv("TEST_SHARD_INDEX", "0", 0);
  setenv("TEST_TOTAL_SHARDS", "1", 0);
  setenv("TEST_SHARD_STATUS_FILE", "", 0);

  return Catch::Session().run(argc, argv);
}

using slangd::test::SimpleTestFixture;

TEST_CASE(
    "Module instance name has self-definition", "[definition][instance]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module counter;
    endmodule

    module top;
      counter cnt_inst ();
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  // Instance name should have self-definition (occurrence 0 is the definition)
  fixture.AssertGoToDefinition(*index, code, "cnt_inst", 0, 0);
}

TEST_CASE(
    "Port connection expressions navigate to variable definitions",
    "[definition][instance]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module register (
      input logic clk_port,
      input logic [7:0] data_port
    );
    endmodule

    module top;
      logic sys_clk;
      logic [7:0] input_data;

      register reg_inst (
        .clk_port(sys_clk),
        .data_port(input_data)
      );
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  // Connection expressions should navigate to their declarations
  // sys_clk: occurrence 0 = definition, occurrence 1 = reference in port
  // connection
  fixture.AssertGoToDefinition(*index, code, "sys_clk", 1, 0);
  // input_data: occurrence 0 = definition, occurrence 1 = reference in port
  // connection
  fixture.AssertGoToDefinition(*index, code, "input_data", 1, 0);
}

TEST_CASE(
    "Parameter assignment expressions navigate to variable definitions",
    "[definition][instance]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module register #(parameter WIDTH = 8) (
      input logic [WIDTH-1:0] data_port
    );
    endmodule

    module top;
      localparam BUS_WIDTH = 16;
      logic [BUS_WIDTH-1:0] data_bus;

      register #(.WIDTH(BUS_WIDTH)) reg_inst (.data_port(data_bus));
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  // BUS_WIDTH in parameter assignment should navigate to its definition
  // occurrence 0 = definition, occurrence 1 = used in data_bus width,
  // occurrence 2 = parameter value
  fixture.AssertGoToDefinition(*index, code, "BUS_WIDTH", 2, 0);
}

TEST_CASE(
    "Parameterized instance with multiple ports and parameters",
    "[definition][instance]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module alu #(parameter DATA_W = 8, parameter OP_W = 4) (
      input logic [DATA_W-1:0] op_a, op_b,
      input logic [OP_W-1:0] operation,
      output logic [DATA_W-1:0] result
    );
    endmodule

    module top;
      localparam WIDTH_PARAM = 32;
      localparam OPCODE_W = 4;
      logic [31:0] operand_a, operand_b, alu_result;
      logic [3:0] alu_op;

      alu #(.DATA_W(WIDTH_PARAM), .OP_W(OPCODE_W)) alu_inst (
        .op_a(operand_a),
        .op_b(operand_b),
        .operation(alu_op),
        .result(alu_result)
      );
    endmodule
  )";

  auto index = fixture.CompileSource(code);

  // Instance name self-definition
  fixture.AssertGoToDefinition(*index, code, "alu_inst", 0, 0);

  // Parameter expressions
  fixture.AssertGoToDefinition(*index, code, "WIDTH_PARAM", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "OPCODE_W", 1, 0);

  // Port connection expressions
  fixture.AssertGoToDefinition(*index, code, "operand_a", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "operand_b", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "alu_op", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "alu_result", 1, 0);
}

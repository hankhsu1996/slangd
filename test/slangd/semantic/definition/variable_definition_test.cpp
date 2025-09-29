#include <cstdlib>
#include <memory>
#include <string>

#include <catch2/catch_all.hpp>
#include <slang/ast/Symbol.h>
#include <slang/util/Enum.h>
#include <spdlog/spdlog.h>

#include "../../common/simple_fixture.hpp"

constexpr auto kLogLevel = spdlog::level::debug;  // Always debug for tests

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

TEST_CASE(
    "SemanticIndex parameter self-definition lookup works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module param_test;
      parameter int WIDTH = 8;
      parameter logic ENABLE = 1'b1;
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "WIDTH", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "ENABLE", 0, 0);
}

TEST_CASE(
    "SemanticIndex variable declaration comprehensive dimension test",
    "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module var_decl_comprehensive;
      localparam int PACKED_W = 8;
      localparam int UNPACKED_W = 16;
      localparam int QUEUE_MAX = 32;

      // Packed dimensions on variable
      logic [PACKED_W-1:0] packed_var;

      // Unpacked dimensions on variable
      logic unpacked_var[UNPACKED_W-1:0];

      // Queue dimension on variable
      int queue_var[$:QUEUE_MAX];
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "PACKED_W", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "UNPACKED_W", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "QUEUE_MAX", 1, 0);
}

TEST_CASE(
    "SemanticIndex multi-dimensional parameter references work",
    "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module multi_dim_test;
      localparam int DIM1 = 4;
      localparam int DIM2 = 8;

      // Multi-dimensional array with parameters
      logic multi_array[DIM1][DIM2-1:0];
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "DIM1", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "DIM2", 1, 0);
}

TEST_CASE(
    "SemanticIndex typedef packed dimensions comprehensive test",
    "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module typedef_packed_comprehensive;
      localparam int WIDTH1 = 8;
      localparam int WIDTH2 = 4;

      // Simple range in packed typedef
      typedef logic [WIDTH1-1:0] simple_packed_t;

      // Ascending range in packed typedef
      typedef logic [0:WIDTH2-1] ascending_packed_t;
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "WIDTH1", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "WIDTH2", 1, 0);
}

TEST_CASE(
    "SemanticIndex typedef unpacked dimensions comprehensive test",
    "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module typedef_unpacked_comprehensive;
      localparam int ARRAY_SIZE = 16;
      localparam int DEPTH = 32;

      // Range select in unpacked typedef
      typedef logic unpacked_range_t[ARRAY_SIZE-1:0];

      // Bit select in unpacked typedef
      typedef int unpacked_bit_t[DEPTH];
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "ARRAY_SIZE", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "DEPTH", 1, 0);
}

TEST_CASE("SemanticIndex port self-definition lookup works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module port_test(
      input  logic clk,
      output logic valid,
      input  logic [31:0] data
    );
      
      // Use the ports in the module
      always_ff @(posedge clk) begin
        valid <= data != 0;
      end
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "clk", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "valid", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "data", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "clk", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "valid", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "data", 1, 0);
}

TEST_CASE(
    "SemanticIndex reference tracking basic functionality", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module test_module;
      logic signal;
      typedef logic [7:0] byte_t;

      initial begin
        signal = 1'b0;  // Reference to signal
      end
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertReferenceExists(*index, code, "signal", 1);
  fixture.AssertGoToDefinition(*index, code, "signal", 1, 0);
}

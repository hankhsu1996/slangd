#include <cstdlib>
#include <memory>
#include <string>

#include <catch2/catch_all.hpp>
#include <slang/ast/Symbol.h>
#include <slang/util/Enum.h>
#include <spdlog/spdlog.h>

#include "../../common/semantic_fixture.hpp"

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

using Fixture = slangd::test::SemanticTestFixture;

TEST_CASE(
    "SemanticIndex parameter self-definition lookup works", "[definition]") {
  std::string code = R"(
    module param_test;
      parameter int WIDTH = 8;
      parameter logic ENABLE = 1'b1;
    endmodule
  )";

  auto result = Fixture::BuildIndex(code);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "WIDTH", 0, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "ENABLE", 0, 0);
}

TEST_CASE(
    "SemanticIndex variable declaration comprehensive dimension test",
    "[definition]") {
  std::string code = R"(
    module var_decl_comprehensive;
      localparam int PACKED_W = 8;
      localparam int UNPACKED_W = 16;
      localparam int QUEUE_MAX = 32;
      localparam int ASSOC_SIZE = 64;

      // Packed dimensions on variable
      logic [PACKED_W-1:0] packed_var;

      // Unpacked dimensions on variable
      logic unpacked_var[UNPACKED_W-1:0];

      // Queue dimension on variable
      int queue_var[$:QUEUE_MAX];

      // Associative array dimension on variable (using type parameter)
      typedef bit [ASSOC_SIZE-1:0] assoc_key_t;
      int assoc_var[assoc_key_t];

      // Dynamic array dimension on variable
      int dynamic_var[];
    endmodule
  )";

  auto result = Fixture::BuildIndex(code);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "PACKED_W", 1, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "UNPACKED_W", 1, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "QUEUE_MAX", 1, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "ASSOC_SIZE", 1, 0);
}

TEST_CASE(
    "SemanticIndex multi-dimensional parameter references work",
    "[definition]") {
  std::string code = R"(
    module multi_dim_test;
      localparam int DIM1 = 4;
      localparam int DIM2 = 8;

      // Multi-dimensional array with parameters
      logic multi_array[DIM1][DIM2-1:0];
    endmodule
  )";

  auto result = Fixture::BuildIndex(code);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "DIM1", 1, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "DIM2", 1, 0);
}

TEST_CASE(
    "SemanticIndex typedef packed dimensions comprehensive test",
    "[definition]") {
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

  auto result = Fixture::BuildIndex(code);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "WIDTH1", 1, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "WIDTH2", 1, 0);
}

TEST_CASE(
    "SemanticIndex typedef unpacked dimensions comprehensive test",
    "[definition]") {
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

  auto result = Fixture::BuildIndex(code);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "ARRAY_SIZE", 1, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "DEPTH", 1, 0);
}

TEST_CASE("SemanticIndex port self-definition lookup works", "[definition]") {
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

  auto result = Fixture::BuildIndex(code);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "clk", 0, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "valid", 0, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "data", 0, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "clk", 1, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "valid", 1, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "data", 1, 0);
}

TEST_CASE(
    "SemanticIndex reference tracking basic functionality", "[definition]") {
  std::string code = R"(
    module test_module;
      logic signal;
      typedef logic [7:0] byte_t;

      initial begin
        signal = 1'b0;  // Reference to signal
      end
    endmodule
  )";

  auto result = Fixture::BuildIndex(code);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "signal", 1, 0);
}

TEST_CASE(
    "SemanticIndex port variable type and parameter disambiguation",
    "[definition]") {
  std::string code = R"(
    typedef logic [7:0] control_t;

    module test_module #(
      parameter WIDTH = 4
    ) (
      input  control_t  [WIDTH-1:0]  control_array
    );
    endmodule
  )";

  auto result = Fixture::BuildIndex(code);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "control_t", 1, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "WIDTH", 1, 0);
}

TEST_CASE(
    "SemanticIndex typedef reference in packed array port variables",
    "[definition]") {
  std::string code = R"(
    typedef struct packed {
      logic [7:0] data;
    } packet_t;

    module test_module (
      output packet_t    simple_output,
      input  packet_t    [3:0] packed_array
    );
    endmodule
  )";

  auto result = Fixture::BuildIndex(code);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "packet_t", 1, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "packet_t", 2, 0);
}

TEST_CASE(
    "SemanticIndex typedef reference in multi-dimensional port variables",
    "[definition]") {
  std::string code = R"(
    typedef struct packed {
      logic [7:0] data;
      logic valid;
    } data_t;

    module test_module (
      output data_t    simple_output,
      input  data_t    [3:0] single_array,
      input  data_t    [3:0][1:0] multi_array
    );
    endmodule
  )";

  auto result = Fixture::BuildIndex(code);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "data_t", 1, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "data_t", 2, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "data_t", 3, 0);
}

TEST_CASE(
    "SemanticIndex variable inside always_comb block works", "[definition]") {
  std::string code = R"(
    module always_test;
      logic [7:0] my_var;
      logic [7:0] other_var;

      always_comb begin
        other_var = my_var;
      end
    endmodule
  )";

  auto result = Fixture::BuildIndex(code);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "my_var", 1, 0);
}

TEST_CASE(
    "SemanticIndex multiple variables on same line with parameter in type",
    "[definition]") {
  std::string code = R"(
    module multi_var_test;
      parameter NUM_ENTRIES = 8;

      // Two variables declared on same line with parameter in type
      logic [NUM_ENTRIES-1:0] var_a, var_b;
    endmodule
  )";

  auto result = Fixture::BuildIndex(code);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "NUM_ENTRIES", 1, 0);
}

TEST_CASE(
    "SemanticIndex enum value in parameter initializer works", "[definition]") {
  std::string code = R"(
    typedef enum {
      MODE_A,
      MODE_B,
      MODE_C
    } mode_t;

    module test #(
      parameter mode_t DEFAULT_MODE = MODE_A
    ) ();
    endmodule
  )";

  auto result = Fixture::BuildIndex(code);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "MODE_A", 0, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "MODE_A", 1, 0);
}

TEST_CASE(
    "SemanticIndex parameter reference in ternary expression works",
    "[definition]") {
  std::string code = R"(
    package config_pkg;
      parameter int VAL_A = 4;
      parameter int VAL_B = 8;
    endpackage

    module test
      import config_pkg::*;
    #(
      parameter bit SELECT = 0,
      localparam int RESULT = SELECT ? VAL_A : VAL_B
    ) ();
    endmodule
  )";

  auto result = Fixture::BuildIndex(code);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "VAL_A", 0, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "VAL_B", 0, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "VAL_A", 1, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "VAL_B", 1, 0);
}

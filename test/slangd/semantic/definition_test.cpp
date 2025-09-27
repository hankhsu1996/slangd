#include <cstdlib>
#include <memory>
#include <string>

#include <catch2/catch_all.hpp>
#include <slang/ast/Symbol.h>
#include <slang/util/Enum.h>
#include <spdlog/spdlog.h>

#include "../common/simple_fixture.hpp"

constexpr auto kLogLevel = spdlog::level::warn;

auto main(int argc, char* argv[]) -> int {
  spdlog::set_level(kLogLevel);
  spdlog::set_pattern("[%l] %v");

  // Suppress Bazel test sharding warnings
  setenv("TEST_SHARD_INDEX", "0", 0);
  setenv("TEST_TOTAL_SHARDS", "1", 0);
  setenv("TEST_SHARD_STATUS_FILE", "", 0);

  return Catch::Session().run(argc, argv);
}

namespace slangd::semantic {

using slangd::test::SimpleTestFixture;

TEST_CASE("SemanticIndex module self-definition lookup works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module empty_module;
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "empty_module", 0, 0);
}

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
    "SemanticIndex typedef self-definition lookup works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module typedef_test;
      typedef logic [7:0] byte_t;
      typedef logic [15:0] word_t;
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "byte_t", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "word_t", 0, 0);
}

TEST_CASE("SemanticIndex type cast reference lookup works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module typecast_test;
      typedef logic [7:0] unique_cast_type;
      logic [7:0] result;

      always_comb begin
        result = unique_cast_type'(8'h42);
      end
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "unique_cast_type", 1, 0);
}

TEST_CASE(
    "SemanticIndex variable declaration parameter reference works",
    "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module param_ref_test;
      localparam int BUS_WIDTH = 8;
      logic [BUS_WIDTH-1:0] data_bus;
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "BUS_WIDTH", 1, 0);
}

TEST_CASE(
    "SemanticIndex unpacked variable dimension parameter reference works",
    "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module unpacked_test;
      localparam int ARRAY_SIZE = 16;
      logic data_array[ARRAY_SIZE-1:0];
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "ARRAY_SIZE", 1, 0);
}

TEST_CASE(
    "SemanticIndex bit select dimension parameter reference works",
    "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module bit_select_test;
      localparam int INDEX_WIDTH = 4;
      logic bit_array[INDEX_WIDTH];
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "INDEX_WIDTH", 1, 0);
}

TEST_CASE(
    "SemanticIndex ascending range dimension parameter reference works",
    "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module ascending_test;
      localparam int WIDTH = 8;
      logic [0:WIDTH-1] ascending_bus;
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "WIDTH", 1, 0);
}

TEST_CASE(
    "SemanticIndex queue dimension parameter reference works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module queue_test;
      localparam int MAX_QUEUE_SIZE = 16;
      int bounded_queue[$:MAX_QUEUE_SIZE];
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "MAX_QUEUE_SIZE", 1, 0);
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
  // Test both width parameters are found in their respective typedef usages
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
    "SemanticIndex packed typedef parameter reference works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module test_packed;
      localparam int PACKED_WIDTH = 8;
      typedef logic [PACKED_WIDTH-1:0] packed_bus_t;
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "PACKED_WIDTH", 1, 0);
}

TEST_CASE(
    "SemanticIndex unpacked typedef parameter go-to-definition",
    "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module test_unpacked_dims;
      localparam int ARRAY_SIZE = 16;
      typedef logic unpacked_array_t[ARRAY_SIZE-1:0];
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "ARRAY_SIZE", 1, 0);
}

TEST_CASE("Parameter self-definition lookup works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module test;
      parameter int WIDTH = 8;
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "WIDTH", 0, 0);
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

TEST_CASE("Module self-definition lookup works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module test_module;
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "test_module", 0, 0);
}

}  // namespace slangd::semantic

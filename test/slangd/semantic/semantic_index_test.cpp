#include "slangd/semantic/semantic_index.hpp"

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

// Helper function to get consistent test URI
inline auto GetTestUri() -> std::string {
  return "file:///test.sv";
}

TEST_CASE("SemanticIndex provides O(1) symbol lookup", "[semantic_index]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module test_module;
      logic test_signal;
      typedef logic [7:0] byte_t;
    endmodule
  )";

  auto index = fixture.CompileSource(code);

  fixture.AssertSymbolAtLocation(
      *index, code, "test_signal", lsp::SymbolKind::kVariable);
}

TEST_CASE("SemanticIndex invalid location lookup", "[semantic_index]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module simple;
    endmodule
  )";

  auto index = fixture.CompileSource(code);

  auto invalid_lookup = index->GetSymbolAt(slang::SourceLocation());
  REQUIRE(!invalid_lookup.has_value());
}

TEST_CASE("SemanticIndex tracks references correctly", "[semantic_index]") {
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

TEST_CASE("SemanticIndex basic symbol lookup", "[semantic_index]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module test_module_unique;
      logic test_signal;
    endmodule
  )";

  auto index = fixture.CompileSource(code);

  // Test basic symbol lookup functionality
  fixture.AssertSymbolAtLocation(
      *index, code, "test_signal", lsp::SymbolKind::kVariable);
}

TEST_CASE(
    "SemanticIndex LookupDefinitionAt method exists and returns optional",
    "[semantic_index]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module test_module;
      logic signal;
    endmodule
  )";

  auto index = fixture.CompileSource(code);

  auto result = index->LookupDefinitionAt(slang::SourceLocation());
  REQUIRE(!result.has_value());
}

TEST_CASE(
    "SemanticIndex module self-definition lookup works", "[semantic_index]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module empty_module;
    endmodule
  )";

  auto index = fixture.CompileSource(code);

  fixture.AssertGoToDefinition(*index, code, "empty_module", 0, 0);
}

TEST_CASE(
    "SemanticIndex parameter self-definition lookup works",
    "[semantic_index]") {
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
    "SemanticIndex typedef self-definition lookup works", "[semantic_index]") {
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

TEST_CASE(
    "SemanticIndex type cast reference lookup works", "[semantic_index]") {
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

// TODO: Variable declaration parameter references require VariableSymbol
// handler Currently only typedef parameter references are implemented
/*
TEST_CASE(
    "SemanticIndex parameter reference go-to-definition works",
    "[semantic_index]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module param_ref_test;
      localparam int BUS_WIDTH = 8;
      logic [BUS_WIDTH-1:0] data_bus;
    endmodule
  )";

  auto index = fixture.CompileSource(code);

  // The infrastructure for typedef parameter references is complete,
  // but this test uses a variable declaration (not typedef).
  // Variable dimension expressions need different handling.

  // Test go-to-definition: parameter usage should resolve to parameter
  // definition BUS_WIDTH occurs at:
  //   [0] localparam definition
  //   [1] usage in variable declaration
  fixture.AssertGoToDefinition(*index, code, "BUS_WIDTH", 1, 0);
}
*/

TEST_CASE(
    "SemanticIndex packed typedef parameter reference works",
    "[semantic_index]") {
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
    "[semantic_index]") {
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

}  // namespace slangd::semantic

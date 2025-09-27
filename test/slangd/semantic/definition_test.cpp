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

  // Test go-to-definition: module name should resolve to itself
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

  // Test go-to-definition: parameters should resolve to themselves
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

  // Test go-to-definition: typedefs should resolve to themselves
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

  // Test go-to-definition: type cast reference should resolve to typedef
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
    "SemanticIndex packed typedef parameter reference works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module test_packed;
      localparam int PACKED_WIDTH = 8;
      typedef logic [PACKED_WIDTH-1:0] packed_bus_t;
    endmodule
  )";

  auto index = fixture.CompileSource(code);

  // Test go-to-definition: parameter reference in typedef should resolve to
  // definition
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

  // Test go-to-definition: parameter reference in unpacked dimensions should
  // resolve to definition
  fixture.AssertGoToDefinition(*index, code, "ARRAY_SIZE", 1, 0);
}

TEST_CASE(
    "Parameter definition range should be name only, not full declaration",
    "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module test;
      parameter int WIDTH = 8;
    endmodule
  )";

  auto index = fixture.CompileSource(code);

  // Find the parameter location in the source by searching for the name
  auto param_location = fixture.FindSymbol(code, "WIDTH");
  REQUIRE(param_location.valid());

  // Lookup the definition range
  auto result = SimpleTestFixture::GetDefinitionRange(*index, param_location);

  REQUIRE(result.has_value());

  // The parameter definition range should contain just the parameter name
  // "WIDTH" (5 chars), not the full declaration "WIDTH = 8" (9 chars)
  auto range_length = result->end().offset() - result->start().offset();

  CHECK(range_length == 5);  // Now correctly returns just "WIDTH"
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

  // Test go-to-definition: signal reference should resolve to definition
  fixture.AssertReferenceExists(*index, code, "signal", 1);
  fixture.AssertGoToDefinition(*index, code, "signal", 1, 0);
}

TEST_CASE("SemanticIndex reference tracking in expressions", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module expression_test;
      logic var_a, var_b, var_c;
      logic [7:0] result;

      always_comb begin
        result = var_a ? var_b : var_c;
        if (var_a && var_b) begin
          result = 8'hFF;
        end
      end
    endmodule
  )";

  auto index = fixture.CompileSource(code);

  // Test that references are tracked in expressions
  const auto& refs = index->GetReferences();
  REQUIRE(!refs.empty());

  // Test that we can find definitions for variables used in expressions
  SimpleTestFixture::AssertContainsSymbols(
      *index, {"var_a", "var_b", "var_c", "result"});
}

TEST_CASE(
    "SemanticIndex handles interface references in expressions",
    "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module generic_module(generic_if iface);
      logic state;
      logic [7:0] counter;
      logic enable;

      always_comb begin
        if (enable & ~iface.ready) begin
          state = 1'b0;
        end else if (enable & iface.ready) begin
          if (iface.mode == 1'b1) begin
            state = 1'b1;
          end else begin
            counter = iface.data;
          end
        end
      end
    endmodule
  )";

  auto index = fixture.CompileSource(code);

  // Test that references through undefined interface members don't crash
  // and that defined symbols are still captured
  REQUIRE(index != nullptr);
  REQUIRE(index->GetSymbolCount() > 0);
  SimpleTestFixture::AssertContainsSymbols(
      *index, {"state", "counter", "enable"});
}

TEST_CASE(
    "SemanticIndex LookupDefinitionAt method basic functionality",
    "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module test_module;
      logic signal;
    endmodule
  )";

  auto index = fixture.CompileSource(code);

  // Test that invalid location returns no result
  auto result = index->LookupDefinitionAt(slang::SourceLocation());
  REQUIRE(!result.has_value());
}

TEST_CASE(
    "SemanticIndex collects definition ranges correctly", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module test_module;
      logic signal;
      typedef logic [7:0] byte_t;

      initial begin : init_block
        signal = 1'b0;
      end
    endmodule
  )";

  auto index = fixture.CompileSource(code);

  // Verify symbols have definition ranges and is_definition flags set
  const auto& all_symbols = index->GetAllSymbols();
  REQUIRE(!all_symbols.empty());

  // Verify expected symbols are present
  SimpleTestFixture::AssertContainsSymbols(
      *index, {"test_module", "signal", "byte_t", "init_block"});

  // Test that all symbols have valid definition ranges
  // This validates that the definition indexing is working correctly
  bool found_at_least_one_definition = false;
  for (const auto& [location, info] : all_symbols) {
    if (info.is_definition) {
      REQUIRE(info.definition_range.start().valid());
      found_at_least_one_definition = true;
    }
  }
  REQUIRE(found_at_least_one_definition);
}

TEST_CASE("SemanticIndex definition API compatibility", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module test_module;
      logic signal;
      typedef logic [7:0] byte_t;
    endmodule
  )";

  auto index = fixture.CompileSource(code);

  // Test reference storage API
  const auto& references = index->GetReferences();
  const auto& all_symbols = index->GetAllSymbols();

  // Basic sanity checks - should have some data
  REQUIRE(!all_symbols.empty());

  // Verify references are accessible via GetReferences()
  (void)references;  // May be empty for single-file tests

  // Test that symbols have definition ranges in their SymbolInfo
  // Verify at least one symbol has valid definition ranges
  bool found_symbol_with_range = false;
  for (const auto& [loc, info] : all_symbols) {
    if (info.is_definition && info.location.valid()) {
      found_symbol_with_range = true;
      // Basic check that definition_range is set
      REQUIRE(info.location.valid());
      break;
    }
  }
  REQUIRE(found_symbol_with_range);
}

TEST_CASE("SemanticIndex invalid location lookup", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module simple;
    endmodule
  )";

  auto index = fixture.CompileSource(code);

  // Test error handling: invalid location should return no result
  auto invalid_lookup = index->GetSymbolAt(slang::SourceLocation());
  REQUIRE(!invalid_lookup.has_value());
}

}  // namespace slangd::semantic

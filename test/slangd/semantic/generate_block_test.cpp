#include <algorithm>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_all.hpp>
#include <slang/ast/Symbol.h>
#include <slang/util/Enum.h>
#include <spdlog/spdlog.h>

#include "../common/simple_fixture.hpp"
#include "slangd/semantic/semantic_index.hpp"

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

TEST_CASE(
    "SemanticIndex collects symbols inside generate if blocks",
    "[generate_blocks]") {
  std::string code = R"(
    module test_gen;
      generate
        if (1) begin : gen_block
          logic gen_signal;
          parameter int GEN_PARAM = 42;
        end
      endgenerate
    endmodule
  )";

  SimpleTestFixture fixture;
  auto index = fixture.CompileSource(code);
  auto symbols = index->GetDocumentSymbols(GetTestUri());

  // Find generate block and verify it contains both signal and parameter
  auto gen_block = std::find_if(
      symbols[0].children->begin(), symbols[0].children->end(),
      [](const auto& s) { return s.name == "gen_block"; });

  REQUIRE(gen_block != symbols[0].children->end());
  REQUIRE(gen_block->children.has_value());
  REQUIRE(gen_block->children->size() == 2);
}

TEST_CASE(
    "SemanticIndex collects symbols inside generate for loops",
    "[generate_blocks]") {
  std::string code = R"(
    module test_gen_for;
      generate
        for (genvar i = 0; i < 4; i++) begin : gen_loop
          logic loop_signal;
          parameter int LOOP_PARAM = 99;
        end
      endgenerate
    endmodule
  )";

  SimpleTestFixture fixture;
  auto index = fixture.CompileSource(code);
  auto symbols = index->GetDocumentSymbols(GetTestUri());

  // Find generate for loop block and verify it contains template symbols
  auto gen_loop = std::find_if(
      symbols[0].children->begin(), symbols[0].children->end(),
      [](const auto& s) { return s.name == "gen_loop"; });

  REQUIRE(gen_loop != symbols[0].children->end());
  REQUIRE(gen_loop->children.has_value());
  // Generate for loop should show meaningful symbols only (genvar filtered out)
  // Expected: loop_signal and LOOP_PARAM (genvar 'i' filtered out)
  REQUIRE(gen_loop->children->size() == 2);

  // Verify we have both loop_signal and LOOP_PARAM, but not the genvar 'i'
  std::vector<std::string> child_names;
  for (const auto& child : *gen_loop->children) {
    child_names.push_back(child.name);
  }

  REQUIRE(
      std::find(child_names.begin(), child_names.end(), "loop_signal") !=
      child_names.end());
  REQUIRE(
      std::find(child_names.begin(), child_names.end(), "LOOP_PARAM") !=
      child_names.end());
  REQUIRE(
      std::find(child_names.begin(), child_names.end(), "i") ==
      child_names.end());
}

TEST_CASE(
    "SemanticIndex filters out truly empty generate blocks",
    "[generate_blocks]") {
  std::string code = R"(
    module test_empty_gen;
      parameter int WIDTH = 4;
      generate
        for (genvar i = 0; i < WIDTH; i++) begin : truly_empty_block
          // Truly empty - no variables, assertions, or other symbols
        end
      endgenerate
    endmodule
  )";

  SimpleTestFixture fixture;
  auto index = fixture.CompileSource(code);
  auto symbols = index->GetDocumentSymbols(GetTestUri());

  // Should have test_empty_gen module but no truly_empty_block namespace
  REQUIRE(symbols.size() == 1);
  REQUIRE(symbols[0].name == "test_empty_gen");

  // The truly empty generate block should be filtered out
  if (symbols[0].children.has_value()) {
    for (const auto& child : *symbols[0].children) {
      REQUIRE(child.name != "truly_empty_block");
    }
  }
}

TEST_CASE(
    "SemanticIndex preserves generate blocks with assertions",
    "[generate_blocks]") {
  std::string code = R"(
    module test_assertion_gen;
      parameter int WIDTH = 4;
      logic clk;
      logic [WIDTH-1:0] data;
      generate
        for (genvar i = 0; i < WIDTH; i++) begin : assertion_block
          // Contains assertion - should not be filtered out
          check_value: assert property (@(posedge clk) data[i] >= 0)
            else $error("Value check failed at index %0d", i);
        end
      endgenerate
    endmodule
  )";

  SimpleTestFixture fixture;
  auto index = fixture.CompileSource(code);
  auto symbols = index->GetDocumentSymbols(GetTestUri());

  // Should have test_assertion_gen module AND assertion_block namespace
  REQUIRE(symbols.size() == 1);
  REQUIRE(symbols[0].name == "test_assertion_gen");

  // The generate block with assertions should NOT be filtered out
  REQUIRE(symbols[0].children.has_value());

  auto assertion_block = std::find_if(
      symbols[0].children->begin(), symbols[0].children->end(),
      [](const auto& s) { return s.name == "assertion_block"; });

  REQUIRE(assertion_block != symbols[0].children->end());
  REQUIRE(assertion_block->kind == lsp::SymbolKind::kNamespace);

  // The assertion block should contain the assertion symbol
  REQUIRE(assertion_block->children.has_value());

  auto check_value = std::find_if(
      assertion_block->children->begin(), assertion_block->children->end(),
      [](const auto& s) { return s.name == "check_value"; });

  REQUIRE(check_value != assertion_block->children->end());
  REQUIRE(
      check_value->kind ==
      lsp::SymbolKind::kVariable);  // Assertions are indexed as variables
}

TEST_CASE(
    "SemanticIndex properly handles assertion symbols in generate blocks",
    "[generate_blocks]") {
  std::string code = R"(
    module test_empty_gen;
      parameter int WIDTH = 4;
      logic clk;
      logic [WIDTH-1:0] data;
      generate
        for (genvar i = 0; i < WIDTH; i++) begin : assertion_block
          // Named assertion should be indexed as a proper symbol
          check_value: assert property (@(posedge clk) data[i] >= 0)
            else $error("Value check failed at index %0d", i);
        end
      endgenerate
    endmodule
  )";

  SimpleTestFixture fixture;
  auto index = fixture.CompileSource(code);
  auto symbols = index->GetDocumentSymbols(GetTestUri());

  // Should have test_empty_gen module
  REQUIRE(symbols.size() == 1);
  REQUIRE(symbols[0].name == "test_empty_gen");

  // The generate block should NOT be filtered out because it contains
  // assertions
  REQUIRE(symbols[0].children.has_value());

  auto assertion_block = std::find_if(
      symbols[0].children->begin(), symbols[0].children->end(),
      [](const auto& s) { return s.name == "assertion_block"; });

  REQUIRE(assertion_block != symbols[0].children->end());
  REQUIRE(assertion_block->kind == lsp::SymbolKind::kNamespace);

  // The assertion block should contain the assertion symbol
  REQUIRE(assertion_block->children.has_value());

  auto check_value = std::find_if(
      assertion_block->children->begin(), assertion_block->children->end(),
      [](const auto& s) { return s.name == "check_value"; });

  REQUIRE(check_value != assertion_block->children->end());
  // Assertions should be classified as variables (or similar, not 'object')
  // NOTE: This should be kVariable or a proper assertion kind, not kObject
  REQUIRE(check_value->kind != lsp::SymbolKind::kObject);
}

}  // namespace slangd::semantic

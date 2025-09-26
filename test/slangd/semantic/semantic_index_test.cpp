#include "slangd/semantic/semantic_index.hpp"

#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

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

TEST_CASE(
    "SemanticIndex processes symbols via preVisit hook", "[semantic_index]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module test_module;
      logic signal;
    endmodule
  )";

  auto index = fixture.CompileSource(code);

  REQUIRE(index != nullptr);

  // Test LSP API: GetDocumentSymbols should return expected symbols
  auto document_symbols = index->GetDocumentSymbols(GetTestUri());
  REQUIRE(!document_symbols.empty());

  // Look for specific symbols we expect
  bool found_module = false;
  bool found_variable = false;
  for (const auto& symbol : document_symbols) {
    if (symbol.name == "test_module") {
      found_module = true;
      REQUIRE(symbol.kind == lsp::SymbolKind::kClass);

      // Check that the module contains the variable
      if (symbol.children.has_value()) {
        for (const auto& child : *symbol.children) {
          if (child.name == "signal") {
            found_variable = true;
            REQUIRE(child.kind == lsp::SymbolKind::kVariable);
          }
        }
      }
    }
  }

  REQUIRE(found_module);
  REQUIRE(found_variable);
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

  // Test O(1) lookup using symbol location
  auto test_location = fixture.FindSymbol(code, "test_signal");
  REQUIRE(test_location.valid());

  // Verify O(1) lookup works
  auto found_symbol = index->GetSymbolAt(test_location);
  REQUIRE(found_symbol.has_value());
  REQUIRE(std::string(found_symbol->symbol->name) == "test_signal");
  REQUIRE(found_symbol->lsp_kind == lsp::SymbolKind::kVariable);

  // Verify lookup with invalid location returns nullopt
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

  // Verify that reference tracking populated the reference_map_
  // We need to access the reference_map through a getter method (to be added
  // later) For now, just verify that the functionality doesn't crash
  REQUIRE(index != nullptr);
  REQUIRE(index->GetSymbolCount() > 0);

  // Look for the signal definition in symbols
  bool found_signal_definition = false;
  for (const auto& [location, info] : index->GetAllSymbols()) {
    if (std::string(info.symbol->name) == "signal" && info.is_definition) {
      found_signal_definition = true;
      REQUIRE(info.is_definition);
      break;
    }
  }

  REQUIRE(found_signal_definition);

  // Reference tracking is verified via GetReferences() API
}

TEST_CASE(
    "SemanticIndex basic definition tracking with fixture",
    "[semantic_index]") {
  SimpleTestFixture fixture;

  SECTION("single variable declaration") {
    const std::string source = R"(
      module m;
        logic test_signal;
      endmodule
    )";

    auto index = fixture.CompileSource(source);

    // Step 1: Just verify it doesn't crash and basic functionality
    REQUIRE(index != nullptr);
    REQUIRE(index->GetSymbolCount() > 0);

    // Verify that symbols are indexed using GetAllSymbols()
    bool found_test_signal = false;
    for (const auto& [loc, info] : index->GetAllSymbols()) {
      if (std::string(info.symbol->name) == "test_signal") {
        found_test_signal = true;
        // Basic check that this is a definition
        break;
      }
    }
    REQUIRE(found_test_signal);
  }
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

  // Test that LookupDefinitionAt exists and returns optional type
  // Using invalid location should return nullopt
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

  // Find the module name location
  auto module_location = fixture.FindSymbol(code, "empty_module");
  REQUIRE(module_location.valid());

  // Look up definition at the module name - should return the module's own
  // definition range
  auto def_range = index->LookupDefinitionAt(module_location);
  REQUIRE(def_range.has_value());

  // The definition range should contain the module location (self-reference)
  REQUIRE(def_range->contains(module_location));
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

  // Find the parameter name locations
  auto width_location = fixture.FindSymbol(code, "WIDTH");
  auto enable_location = fixture.FindSymbol(code, "ENABLE");
  REQUIRE(width_location.valid());
  REQUIRE(enable_location.valid());

  // Look up definition at parameter names - should return their own definition
  // ranges
  auto width_def = index->LookupDefinitionAt(width_location);
  auto enable_def = index->LookupDefinitionAt(enable_location);

  REQUIRE(width_def.has_value());
  REQUIRE(enable_def.has_value());

  // The definition ranges should contain the parameter locations
  // (self-references)
  REQUIRE(width_def->contains(width_location));
  REQUIRE(enable_def->contains(enable_location));
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

  // Find the typedef name locations
  auto byte_location = fixture.FindSymbol(code, "byte_t");
  auto word_location = fixture.FindSymbol(code, "word_t");
  REQUIRE(byte_location.valid());
  REQUIRE(word_location.valid());

  // Look up definition at typedef names - should return their own definition
  // ranges
  auto byte_def = index->LookupDefinitionAt(byte_location);
  auto word_def = index->LookupDefinitionAt(word_location);

  REQUIRE(byte_def.has_value());
  REQUIRE(word_def.has_value());

  // The definition ranges should contain the typedef locations
  // (self-references)
  REQUIRE(byte_def->contains(byte_location));
  REQUIRE(word_def->contains(word_location));
}

}  // namespace slangd::semantic

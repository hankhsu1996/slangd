#include <algorithm>
#include <cstdlib>
#include <functional>
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
    "SemanticIndex GetDocumentSymbols with enum hierarchy",
    "[document_symbols]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module test_module;
      typedef enum logic [1:0] {
        IDLE,
        ACTIVE,
        DONE
      } state_t;
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  auto symbols = index->GetDocumentSymbols(GetTestUri());

  // Find enum in module and verify it contains enum members
  SimpleTestFixture::AssertDocumentSymbolExists(
      symbols, "state_t", lsp::SymbolKind::kEnum);

  // Find the enum to verify it has the right number of children
  auto enum_symbol = std::find_if(
      symbols[0].children->begin(), symbols[0].children->end(),
      [](const auto& s) { return s.name == "state_t"; });
  REQUIRE(enum_symbol->children.has_value());
  REQUIRE(enum_symbol->children->size() == 3);  // IDLE, ACTIVE, DONE
}

TEST_CASE(
    "SemanticIndex GetDocumentSymbols includes struct fields",
    "[document_symbols]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    package test_pkg;
      typedef struct {
        logic [7:0] data;
        logic valid;
        logic [15:0] address;
      } packet_t;
    endpackage
  )";

  auto index = fixture.CompileSource(code);
  auto symbols = index->GetDocumentSymbols(GetTestUri());

  // Find struct in package and verify it contains struct fields
  SimpleTestFixture::AssertDocumentSymbolExists(
      symbols, "packet_t", lsp::SymbolKind::kStruct);

  // Find the struct to verify it has the right number of children
  auto struct_symbol = std::find_if(
      symbols[0].children->begin(), symbols[0].children->end(),
      [](const auto& s) { return s.name == "packet_t"; });
  REQUIRE(struct_symbol->children.has_value());
  REQUIRE(struct_symbol->children->size() == 3);  // data, valid, address
}

TEST_CASE(
    "SemanticIndex handles symbols with empty names for VSCode compatibility",
    "[document_symbols]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module test_module;
      generate
        if (1) begin
          logic gen_signal;
        end
      endgenerate
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  auto symbols = index->GetDocumentSymbols(GetTestUri());

  // All document symbols should have non-empty names (VSCode requirement)
  std::function<void(const std::vector<lsp::DocumentSymbol>&)> check_names;
  check_names = [&check_names](const std::vector<lsp::DocumentSymbol>& syms) {
    for (const auto& symbol : syms) {
      REQUIRE(!symbol.name.empty());  // VSCode rejects empty names
      if (symbol.children.has_value()) {
        check_names(*symbol.children);
      }
    }
  };

  check_names(symbols);
}

TEST_CASE(
    "SemanticIndex filters out genvar loop variables from document symbols",
    "[document_symbols]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module sub_module;
    endmodule

    module test_module;
      parameter int NUM_ENTRIES = 4;

      generate
        for (genvar entry = 0; entry < NUM_ENTRIES; entry++) begin : gen_loop
          sub_module inst();
          logic local_signal;
        end
      endgenerate
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  auto symbols = index->GetDocumentSymbols(GetTestUri());

  // Check that genvar 'entry' is not in document symbols anywhere
  std::function<void(const std::vector<lsp::DocumentSymbol>&)> check_no_genvar;
  check_no_genvar =
      [&check_no_genvar](const std::vector<lsp::DocumentSymbol>& syms) {
        for (const auto& symbol : syms) {
          // The genvar 'entry' should not appear as a document symbol
          REQUIRE(symbol.name != "entry");

          if (symbol.children.has_value()) {
            check_no_genvar(*symbol.children);
          }
        }
      };

  check_no_genvar(symbols);

  // Verify that other meaningful symbols are still there
  SimpleTestFixture::AssertDocumentSymbolExists(
      symbols, "test_module", lsp::SymbolKind::kClass);
  SimpleTestFixture::AssertDocumentSymbolExists(
      symbols, "gen_loop", lsp::SymbolKind::kNamespace);
  SimpleTestFixture::AssertDocumentSymbolExists(
      symbols, "local_signal", lsp::SymbolKind::kVariable);
}

TEST_CASE(
    "SemanticIndex function internals not in document symbols but available "
    "for goto-definition",
    "[document_symbols]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module test_module;
      function automatic logic my_function();
        logic local_var;
        logic [7:0] local_array;
        local_var = 1'b1;
        my_function = local_var;
      endfunction
    endmodule
  )";

  auto index = fixture.CompileSource(code);

  // Test 1: Document symbols should NOT show function internals
  auto symbols = index->GetDocumentSymbols(GetTestUri());
  REQUIRE(!symbols.empty());
  REQUIRE(symbols[0].children.has_value());

  // Find the function
  SimpleTestFixture::AssertDocumentSymbolExists(
      symbols, "my_function", lsp::SymbolKind::kFunction);

  // Find the function to verify it has no children
  auto function_symbol = std::find_if(
      symbols[0].children->begin(), symbols[0].children->end(),
      [](const auto& s) { return s.name == "my_function"; });

  // Function should be a leaf node - no local_var or local_array in document
  // symbols
  REQUIRE(
      (!function_symbol->children.has_value() ||
       function_symbol->children->empty()));

  // Test 2: But local variables should still be in semantic index for
  // go-to-definition
  SimpleTestFixture::AssertContainsSymbols(
      *index, {"local_var", "local_array"});
}

}  // namespace slangd::semantic

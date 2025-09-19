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

auto main(int argc, char* argv[]) -> int {
  if (auto* level = std::getenv("SPDLOG_LEVEL")) {
    spdlog::set_level(spdlog::level::from_str(level));
  } else {
    spdlog::set_level(spdlog::level::warn);
  }
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
  auto enum_symbol = std::find_if(
      symbols[0].children->begin(), symbols[0].children->end(),
      [](const auto& s) { return s.name == "state_t"; });

  REQUIRE(enum_symbol != symbols[0].children->end());
  REQUIRE(enum_symbol->kind == lsp::SymbolKind::kEnum);
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
  auto struct_symbol = std::find_if(
      symbols[0].children->begin(), symbols[0].children->end(),
      [](const auto& s) { return s.name == "packet_t"; });

  REQUIRE(struct_symbol != symbols[0].children->end());
  REQUIRE(struct_symbol->kind == lsp::SymbolKind::kStruct);
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

  // But verify that other meaningful symbols are still there
  bool found_test_module = false;
  bool found_gen_loop = false;
  bool found_local_signal = false;

  std::function<void(const std::vector<lsp::DocumentSymbol>&)>
      check_meaningful_symbols;
  check_meaningful_symbols = [&](const std::vector<lsp::DocumentSymbol>& syms) {
    for (const auto& symbol : syms) {
      if (symbol.name == "test_module") {
        found_test_module = true;
      }
      if (symbol.name == "gen_loop") {
        found_gen_loop = true;
      }
      if (symbol.name == "local_signal") {
        found_local_signal = true;
      }

      if (symbol.children.has_value()) {
        check_meaningful_symbols(*symbol.children);
      }
    }
  };

  check_meaningful_symbols(symbols);

  // Verify meaningful symbols are present while genvar is filtered out
  REQUIRE(found_test_module);
  REQUIRE(found_gen_loop);
  REQUIRE(found_local_signal);
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
  auto function_symbol = std::find_if(
      symbols[0].children->begin(), symbols[0].children->end(),
      [](const auto& s) { return s.name == "my_function"; });

  REQUIRE(function_symbol != symbols[0].children->end());
  REQUIRE(function_symbol->kind == lsp::SymbolKind::kFunction);

  // Function should be a leaf node - no local_var or local_array in document
  // symbols
  REQUIRE(
      (!function_symbol->children.has_value() ||
       function_symbol->children->empty()));

  // Test 2: But local variables should still be in semantic index for
  // go-to-definition
  const auto& all_symbols = index->GetAllSymbols();

  bool found_local_var = false;
  bool found_local_array = false;
  for (const auto& [location, info] : all_symbols) {
    std::string name(info.symbol->name);
    if (name == "local_var") {
      found_local_var = true;
    }
    if (name == "local_array") {
      found_local_array = true;
    }
  }

  // Local variables should be indexed for go-to-definition functionality
  REQUIRE(found_local_var);
  REQUIRE(found_local_array);
}

}  // namespace slangd::semantic

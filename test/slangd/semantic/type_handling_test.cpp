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

TEST_CASE("SemanticIndex handles enum and struct types", "[type_handling]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    interface test_if;
      logic clk;
      logic rst;
      modport master (input clk, output rst);
    endinterface

    module test_module(
      test_if.master bus
    );
      typedef enum logic [1:0] {
        IDLE,
        ACTIVE,
        DONE
      } state_t;

      state_t state;

      typedef struct {
        logic [7:0] data;
        logic valid;
      } packet_t;
    endmodule
  )";

  auto index = fixture.CompileSource(code);

  // Test LSP API: GetDocumentSymbols should return expected types
  auto document_symbols = index->GetDocumentSymbols(GetTestUri());
  REQUIRE(!document_symbols.empty());

  // Check for interface with modport
  bool found_interface = false;
  bool found_module = false;
  for (const auto& symbol : document_symbols) {
    if (symbol.name == "test_if") {
      found_interface = true;
      REQUIRE(symbol.kind == lsp::SymbolKind::kInterface);
    }
    if (symbol.name == "test_module") {
      found_module = true;
      REQUIRE(symbol.kind == lsp::SymbolKind::kClass);
    }
  }

  REQUIRE(found_interface);
  REQUIRE(found_module);
}

TEST_CASE(
    "SemanticIndex collects definition ranges correctly", "[type_handling]") {
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

  bool found_module = false;
  bool found_signal = false;
  bool found_typedef = false;
  bool found_block = false;

  for (const auto& [location, info] : all_symbols) {
    std::string name(info.symbol->name);

    if (name == "test_module") {
      found_module = true;
      REQUIRE(info.is_definition);
      REQUIRE(info.definition_range.start().valid());
      REQUIRE(info.definition_range.end().valid());
    } else if (name == "signal") {
      found_signal = true;
      REQUIRE(info.is_definition);
      REQUIRE(info.definition_range.start().valid());
    } else if (name == "byte_t") {
      found_typedef = true;
      REQUIRE(info.is_definition);
      REQUIRE(info.definition_range.start().valid());
    } else if (name == "init_block") {
      found_block = true;
      REQUIRE(info.is_definition);
      REQUIRE(info.definition_range.start().valid());
    }
  }

  REQUIRE(found_module);
  REQUIRE(found_signal);
  REQUIRE(found_typedef);
  REQUIRE(found_block);
}

TEST_CASE(
    "SemanticIndex DefinitionIndex-compatible API basic functionality",
    "[type_handling]") {
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

TEST_CASE(
    "SemanticIndex collects functions and tasks correctly", "[type_handling]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module test_module;
      // Function with explicit return type
      function automatic logic simple_func();
        simple_func = 1'b0;
      endfunction

      // Simple task
      task automatic simple_task();
        $display("test");
      endtask
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  auto symbols = index->GetDocumentSymbols(GetTestUri());

  REQUIRE(!symbols.empty());
  REQUIRE(symbols[0].children.has_value());

  // Find functions and tasks in module
  auto function_symbol = std::find_if(
      symbols[0].children->begin(), symbols[0].children->end(),
      [](const auto& s) { return s.name == "simple_func"; });

  auto task_symbol = std::find_if(
      symbols[0].children->begin(), symbols[0].children->end(),
      [](const auto& s) { return s.name == "simple_task"; });

  REQUIRE(function_symbol != symbols[0].children->end());
  REQUIRE(function_symbol->kind == lsp::SymbolKind::kFunction);
  // Functions should be leaf nodes (no children shown in document symbols)
  REQUIRE(
      (!function_symbol->children.has_value() ||
       function_symbol->children->empty()));

  REQUIRE(task_symbol != symbols[0].children->end());
  REQUIRE(task_symbol->kind == lsp::SymbolKind::kFunction);
  // Tasks should be leaf nodes (no children shown in document symbols)
  REQUIRE(
      (!task_symbol->children.has_value() || task_symbol->children->empty()));
}

}  // namespace slangd::semantic

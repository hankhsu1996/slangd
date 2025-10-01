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
    "SemanticIndex ShouldIndexForDocumentSymbols filters genvar correctly",
    "[document_symbols]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module test_module;
      logic signal;
      generate
        for (genvar i = 0; i < 4; i++) begin : gen_block
          logic loop_signal;
        end
      endgenerate
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  auto symbols = index->GetDocumentSymbols(GetTestUri());

  // Genvar 'i' should be filtered out of document symbols
  std::function<void(const std::vector<lsp::DocumentSymbol>&)> check_no_genvar;
  check_no_genvar =
      [&check_no_genvar](const std::vector<lsp::DocumentSymbol>& syms) {
        for (const auto& symbol : syms) {
          REQUIRE(symbol.name != "i");
          if (symbol.children.has_value()) {
            check_no_genvar(*symbol.children);
          }
        }
      };

  check_no_genvar(symbols);

  // But meaningful symbols should still be there
  SimpleTestFixture::AssertDocumentSymbolExists(
      symbols, "test_module", lsp::SymbolKind::kClass);
  SimpleTestFixture::AssertDocumentSymbolExists(
      symbols, "signal", lsp::SymbolKind::kVariable);
  SimpleTestFixture::AssertDocumentSymbolExists(
      symbols, "gen_block", lsp::SymbolKind::kNamespace);
}

TEST_CASE(
    "SemanticIndex ConvertToLspKind handles complex type aliases",
    "[document_symbols]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module test_module;
      typedef enum logic [1:0] {
        IDLE, ACTIVE, DONE
      } state_t;

      typedef struct {
        logic [7:0] data;
        logic valid;
      } packet_t;
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  auto symbols = index->GetDocumentSymbols(GetTestUri());

  // Find enum and struct typedefs and verify correct LSP kinds
  std::function<void(const std::vector<lsp::DocumentSymbol>&)> check_symbols;
  check_symbols = [&](const auto& syms) {
    for (const auto& sym : syms) {
      if (sym.name == "state_t") {
        REQUIRE(sym.kind == lsp::SymbolKind::kEnum);
      }
      if (sym.name == "packet_t") {
        REQUIRE(sym.kind == lsp::SymbolKind::kStruct);
      }
      if (sym.children.has_value()) {
        check_symbols(*sym.children);
      }
    }
  };

  check_symbols(symbols);
}

TEST_CASE(
    "SemanticIndex handles nested scope definitions in document symbols",
    "[document_symbols]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module nested_test;
      logic clk;
      if (1) begin : named_block
        logic nested_signal;
        always_ff @(posedge clk) begin
          logic deeply_nested;
        end
      end
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  auto symbols = index->GetDocumentSymbols(GetTestUri());

  // Test document symbol hierarchy for nested scopes
  SimpleTestFixture::AssertDocumentSymbolExists(
      symbols, "nested_test", lsp::SymbolKind::kClass);
  SimpleTestFixture::AssertDocumentSymbolExists(
      symbols, "named_block", lsp::SymbolKind::kNamespace);
  SimpleTestFixture::AssertDocumentSymbolExists(
      symbols, "nested_signal", lsp::SymbolKind::kVariable);
}

TEST_CASE(
    "SemanticIndex handles multiple declarations on single line in document "
    "symbols",
    "[document_symbols]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module multi_decl_test;
      logic sig1, sig2, sig3;
      logic [7:0] byte1, byte2, byte3;
      wire w1, w2, w3;
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  auto symbols = index->GetDocumentSymbols(GetTestUri());

  // Test that all symbols from multi-declarations appear in document symbols
  const std::vector<std::string> expected = {
      "sig1", "sig2", "sig3", "byte1", "byte2", "byte3", "w1", "w2", "w3"};

  for (const auto& symbol_name : expected) {
    SimpleTestFixture::AssertDocumentSymbolExists(
        symbols, symbol_name, lsp::SymbolKind::kVariable);
  }
}

TEST_CASE(
    "SemanticIndex package definitions in document symbols",
    "[document_symbols]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    package test_pkg;
      parameter WIDTH = 32;
      typedef logic [WIDTH-1:0] data_t;
    endpackage
  )";

  auto index = fixture.CompileSource(code);
  auto symbols = index->GetDocumentSymbols(GetTestUri());

  // Test package and its contents appear in document symbols
  SimpleTestFixture::AssertDocumentSymbolExists(
      symbols, "test_pkg", lsp::SymbolKind::kPackage);
  SimpleTestFixture::AssertDocumentSymbolExists(
      symbols, "WIDTH", lsp::SymbolKind::kConstant);
  SimpleTestFixture::AssertDocumentSymbolExists(
      symbols, "data_t", lsp::SymbolKind::kTypeParameter);
}

TEST_CASE(
    "SemanticIndex struct and union types in document symbols",
    "[document_symbols]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module struct_test;
      typedef struct packed {
        logic [7:0] header;
        logic [23:0] payload;
      } packet_t;

      typedef union packed {
        logic [31:0] word;
        logic [7:0][3:0] bytes;
      } data_t;

      packet_t pkt;
      data_t data;
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  auto symbols = index->GetDocumentSymbols(GetTestUri());

  // Test struct/union types and instances appear in document symbols
  SimpleTestFixture::AssertDocumentSymbolExists(
      symbols, "packet_t", lsp::SymbolKind::kStruct);
  SimpleTestFixture::AssertDocumentSymbolExists(
      symbols, "data_t", lsp::SymbolKind::kStruct);
  SimpleTestFixture::AssertDocumentSymbolExists(
      symbols, "pkt", lsp::SymbolKind::kVariable);
  SimpleTestFixture::AssertDocumentSymbolExists(
      symbols, "data", lsp::SymbolKind::kVariable);
}

TEST_CASE(
    "SemanticIndex module with package imports in document symbols",
    "[document_symbols]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    package test_pkg;
      parameter WIDTH = 32;
      typedef logic [WIDTH-1:0] data_t;
    endpackage

    module import_test;
      import test_pkg::*;
      data_t test_signal;
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  auto symbols = index->GetDocumentSymbols(GetTestUri());

  // Test that imported symbols and using module appear in document symbols
  SimpleTestFixture::AssertDocumentSymbolExists(
      symbols, "test_pkg", lsp::SymbolKind::kPackage);
  SimpleTestFixture::AssertDocumentSymbolExists(
      symbols, "import_test", lsp::SymbolKind::kClass);
  SimpleTestFixture::AssertDocumentSymbolExists(
      symbols, "test_signal", lsp::SymbolKind::kVariable);
}

TEST_CASE(
    "SemanticIndex handles interface ports in document symbols",
    "[document_symbols]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    interface cpu_if;
      logic [31:0] addr;
      logic [31:0] data;
      modport master(output addr, data);
    endinterface

    module cpu_core(cpu_if.master bus);
      assign bus.addr = 32'h1000;
      assign bus.data = 32'hDEAD;
      logic internal_var;
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  auto symbols = index->GetDocumentSymbols(GetTestUri());

  // Test that interface and module with interface ports appear in document
  // symbols
  REQUIRE(!symbols.empty());
  SimpleTestFixture::AssertDocumentSymbolExists(
      symbols, "cpu_if", lsp::SymbolKind::kInterface);
  SimpleTestFixture::AssertDocumentSymbolExists(
      symbols, "cpu_core", lsp::SymbolKind::kClass);
  SimpleTestFixture::AssertDocumentSymbolExists(
      symbols, "internal_var", lsp::SymbolKind::kVariable);
}

TEST_CASE(
    "SemanticIndex handles enum and struct types in document symbols",
    "[document_symbols]") {
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
  auto symbols = index->GetDocumentSymbols(GetTestUri());

  // Test LSP API: GetDocumentSymbols should return expected types
  REQUIRE(!symbols.empty());

  // Check for interface with modport and module
  SimpleTestFixture::AssertDocumentSymbolExists(
      symbols, "test_if", lsp::SymbolKind::kInterface);
  SimpleTestFixture::AssertDocumentSymbolExists(
      symbols, "test_module", lsp::SymbolKind::kClass);
}

TEST_CASE(
    "SemanticIndex collects functions and tasks in document symbols",
    "[document_symbols]") {
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
  SimpleTestFixture::AssertDocumentSymbolExists(
      symbols, "simple_func", lsp::SymbolKind::kFunction);
  SimpleTestFixture::AssertDocumentSymbolExists(
      symbols, "simple_task", lsp::SymbolKind::kFunction);

  // Verify function is a leaf node (no children shown in document symbols)
  auto function_symbol = std::find_if(
      symbols[0].children->begin(), symbols[0].children->end(),
      [](const auto& s) -> bool { return s.name == "simple_func"; });
  REQUIRE(
      (!function_symbol->children.has_value() ||
       function_symbol->children->empty()));

  auto task_symbol = std::find_if(
      symbols[0].children->begin(), symbols[0].children->end(),
      [](const auto& s) -> bool { return s.name == "simple_task"; });
  // Tasks should be leaf nodes (no children shown in document symbols)
  REQUIRE(
      (!task_symbol->children.has_value() || task_symbol->children->empty()));
}

TEST_CASE(
    "SemanticIndex collects symbols inside generate if blocks",
    "[document_symbols]") {
  SimpleTestFixture fixture;
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

  auto index = fixture.CompileSource(code);
  auto symbols = index->GetDocumentSymbols(GetTestUri());

  // Test that generate block appears in document symbols with correct children
  SimpleTestFixture::AssertDocumentSymbolExists(
      symbols, "gen_block", lsp::SymbolKind::kNamespace);
  SimpleTestFixture::AssertDocumentSymbolExists(
      symbols, "gen_signal", lsp::SymbolKind::kVariable);
  SimpleTestFixture::AssertDocumentSymbolExists(
      symbols, "GEN_PARAM", lsp::SymbolKind::kConstant);
}

TEST_CASE(
    "SemanticIndex collects symbols inside generate for loops",
    "[document_symbols]") {
  SimpleTestFixture fixture;
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

  auto index = fixture.CompileSource(code);
  auto symbols = index->GetDocumentSymbols(GetTestUri());

  // Test that generate for loop block and its contents appear in document
  // symbols
  SimpleTestFixture::AssertDocumentSymbolExists(
      symbols, "gen_loop", lsp::SymbolKind::kNamespace);
  SimpleTestFixture::AssertDocumentSymbolExists(
      symbols, "loop_signal", lsp::SymbolKind::kVariable);
  SimpleTestFixture::AssertDocumentSymbolExists(
      symbols, "LOOP_PARAM", lsp::SymbolKind::kConstant);
}

TEST_CASE(
    "SemanticIndex filters out truly empty generate blocks",
    "[document_symbols]") {
  SimpleTestFixture fixture;
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

  auto index = fixture.CompileSource(code);
  auto symbols = index->GetDocumentSymbols(GetTestUri());

  // Test that truly empty generate blocks are filtered out of document symbols
  REQUIRE(symbols.size() == 1);
  REQUIRE(symbols[0].name == "test_empty_gen");

  // The truly empty generate block should not appear in document symbols
  std::function<bool(const std::vector<lsp::DocumentSymbol>&)> has_empty_block;
  has_empty_block =
      [&has_empty_block](const std::vector<lsp::DocumentSymbol>& syms) -> bool {
    return std::ranges::any_of(syms, [&](const auto& symbol) -> bool {
      return symbol.name == "truly_empty_block" ||
             (symbol.children.has_value() && has_empty_block(*symbol.children));
    });
  };

  REQUIRE(!has_empty_block(symbols));
}

TEST_CASE(
    "SemanticIndex preserves generate blocks with assertions",
    "[document_symbols]") {
  SimpleTestFixture fixture;
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

  auto index = fixture.CompileSource(code);
  auto symbols = index->GetDocumentSymbols(GetTestUri());

  // Test that generate blocks with assertions appear in document symbols
  SimpleTestFixture::AssertDocumentSymbolExists(
      symbols, "test_assertion_gen", lsp::SymbolKind::kClass);
  SimpleTestFixture::AssertDocumentSymbolExists(
      symbols, "assertion_block", lsp::SymbolKind::kNamespace);
  SimpleTestFixture::AssertDocumentSymbolExists(
      symbols, "check_value", lsp::SymbolKind::kVariable);
}

TEST_CASE(
    "SemanticIndex handles assertion symbols in generate blocks",
    "[document_symbols]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module test_assertion_gen;
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

  auto index = fixture.CompileSource(code);
  auto symbols = index->GetDocumentSymbols(GetTestUri());

  // Test that assertion symbols are properly classified in document symbols
  SimpleTestFixture::AssertDocumentSymbolExists(
      symbols, "assertion_block", lsp::SymbolKind::kNamespace);
  SimpleTestFixture::AssertDocumentSymbolExists(
      symbols, "check_value", lsp::SymbolKind::kVariable);

  // Find the assertion to verify it's not classified as kObject
  std::function<const lsp::DocumentSymbol*(
      const std::vector<lsp::DocumentSymbol>&, const std::string&)>
      find_symbol;
  find_symbol = [&find_symbol](
                    const std::vector<lsp::DocumentSymbol>& syms,
                    const std::string& name) -> const lsp::DocumentSymbol* {
    for (const auto& symbol : syms) {
      if (symbol.name == name) {
        return &symbol;
      }
      if (symbol.children.has_value()) {
        if (const auto* found = find_symbol(*symbol.children, name)) {
          return found;
        }
      }
    }
    return nullptr;
  };

  const auto* check_value = find_symbol(symbols, "check_value");
  REQUIRE(check_value != nullptr);
  REQUIRE(check_value->kind != lsp::SymbolKind::kObject);
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

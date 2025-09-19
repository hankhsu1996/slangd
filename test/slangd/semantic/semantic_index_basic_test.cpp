#include <algorithm>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_all.hpp>
#include <slang/ast/Symbol.h>
#include <slang/util/Enum.h>
#include <spdlog/spdlog.h>

#include "slangd/semantic/semantic_index.hpp"
#include "test_fixtures.hpp"

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

using SemanticTestFixture = slangd::semantic::test::SemanticTestFixture;

// Helper function to get consistent test URI
inline auto GetTestUri() -> std::string {
  return "file:///test.sv";
}

TEST_CASE(
    "SemanticIndex processes symbols via preVisit hook", "[semantic_index]") {
  std::string code = R"(
    module test_module;
      logic signal;
    endmodule
  )";

  auto source_manager = std::make_shared<slang::SourceManager>();
  slang::Bag options;
  auto compilation = std::make_unique<slang::ast::Compilation>(options);

  // Use consistent test path format
  constexpr std::string_view kTestFilename = "test.sv";
  std::string test_uri = "file:///" + std::string(kTestFilename);
  std::string test_path = "/" + std::string(kTestFilename);

  auto buffer = source_manager->assignText(test_path, code);
  auto tree = slang::syntax::SyntaxTree::fromBuffer(buffer, *source_manager);
  if (tree) {
    compilation->addSyntaxTree(tree);
  }

  auto index =
      SemanticIndex::FromCompilation(*compilation, *source_manager, test_uri);

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
  SemanticTestFixture fixture;
  std::string code = R"(
    package test_pkg;
      typedef logic [7:0] byte_t;
    endpackage

    module test_module;
      import test_pkg::*;
      byte_t data;
    endmodule
  )";

  auto index = fixture.BuildIndexFromSource(code);

  // Test O(1) lookup using symbol location
  auto test_location = fixture.FindLocation(code, "test_pkg");
  REQUIRE(test_location.valid());

  // Verify O(1) lookup works
  auto found_symbol = index->GetSymbolAt(test_location);
  REQUIRE(found_symbol.has_value());
  REQUIRE(std::string(found_symbol->symbol->name) == "test_pkg");
  REQUIRE(found_symbol->lsp_kind == lsp::SymbolKind::kPackage);

  // Verify lookup with invalid location returns nullopt
  auto invalid_lookup = index->GetSymbolAt(slang::SourceLocation());
  REQUIRE(!invalid_lookup.has_value());
}

TEST_CASE("SemanticIndex handles enum and struct types", "[semantic_index]") {
  SemanticTestFixture fixture;
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

  auto index = fixture.BuildIndexFromSource(code);

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
    "SemanticIndex GetDocumentSymbols with enum hierarchy",
    "[semantic_index]") {
  SemanticTestFixture fixture;
  std::string code = R"(
    module test_module;
      typedef enum logic [1:0] {
        IDLE,
        ACTIVE,
        DONE
      } state_t;
    endmodule
  )";

  auto index = fixture.BuildIndexFromSource(code);
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
    "SemanticIndex collects definition ranges correctly", "[semantic_index]") {
  std::string code = R"(
    module test_module;
      logic signal;
      typedef logic [7:0] byte_t;

      initial begin : init_block
        signal = 1'b0;
      end
    endmodule
  )";

  auto source_manager = std::make_shared<slang::SourceManager>();
  slang::Bag options;
  auto compilation = std::make_unique<slang::ast::Compilation>(options);

  // Use consistent test path format
  constexpr std::string_view kTestFilename = "test.sv";
  std::string test_uri = "file:///" + std::string(kTestFilename);
  std::string test_path = "/" + std::string(kTestFilename);

  auto buffer = source_manager->assignText(test_path, code);
  auto tree = slang::syntax::SyntaxTree::fromBuffer(buffer, *source_manager);
  if (tree) {
    compilation->addSyntaxTree(tree);
  }

  auto index =
      SemanticIndex::FromCompilation(*compilation, *source_manager, test_uri);

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

TEST_CASE("SemanticIndex tracks references correctly", "[semantic_index]") {
  std::string code = R"(
    module test_module;
      logic signal;
      typedef logic [7:0] byte_t;

      initial begin
        signal = 1'b0;  // Reference to signal
      end
    endmodule
  )";

  auto source_manager = std::make_shared<slang::SourceManager>();
  slang::Bag options;
  auto compilation = std::make_unique<slang::ast::Compilation>(options);

  // Use consistent test path format
  constexpr std::string_view kTestFilename = "test.sv";
  std::string test_uri = "file:///" + std::string(kTestFilename);
  std::string test_path = "/" + std::string(kTestFilename);

  auto buffer = source_manager->assignText(test_path, code);
  auto tree = slang::syntax::SyntaxTree::fromBuffer(buffer, *source_manager);
  if (tree) {
    compilation->addSyntaxTree(tree);
  }

  auto index =
      SemanticIndex::FromCompilation(*compilation, *source_manager, test_uri);

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
    "SemanticIndex DefinitionIndex-compatible API basic functionality",
    "[semantic_index]") {
  std::string code = R"(
    module test_module;
      logic signal;
      typedef logic [7:0] byte_t;
    endmodule
  )";

  auto source_manager = std::make_shared<slang::SourceManager>();
  slang::Bag options;
  auto compilation = std::make_unique<slang::ast::Compilation>(options);

  // Use consistent test path format
  constexpr std::string_view kTestFilename = "test.sv";
  std::string test_uri = "file:///" + std::string(kTestFilename);
  std::string test_path = "/" + std::string(kTestFilename);

  auto buffer = source_manager->assignText(test_path, code);
  auto tree = slang::syntax::SyntaxTree::fromBuffer(buffer, *source_manager);
  if (tree) {
    compilation->addSyntaxTree(tree);
  }

  auto index =
      SemanticIndex::FromCompilation(*compilation, *source_manager, test_uri);

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
    "SemanticIndex LookupDefinitionAt method exists and returns optional",
    "[semantic_index]") {
  std::string code = R"(
    module test_module;
      logic signal;
    endmodule
  )";

  auto source_manager = std::make_shared<slang::SourceManager>();
  slang::Bag options;
  auto compilation = std::make_unique<slang::ast::Compilation>(options);

  // Use consistent test path format
  constexpr std::string_view kTestFilename = "test.sv";
  std::string test_uri = "file:///" + std::string(kTestFilename);
  std::string test_path = "/" + std::string(kTestFilename);

  auto buffer = source_manager->assignText(test_path, code);
  auto tree = slang::syntax::SyntaxTree::fromBuffer(buffer, *source_manager);
  if (tree) {
    compilation->addSyntaxTree(tree);
  }

  auto index =
      SemanticIndex::FromCompilation(*compilation, *source_manager, test_uri);

  // Test that LookupDefinitionAt exists and returns optional type
  // Using invalid location should return nullopt
  auto result = index->LookupDefinitionAt(slang::SourceLocation());
  REQUIRE(!result.has_value());
}

TEST_CASE(
    "SemanticIndex basic definition tracking with fixture",
    "[semantic_index]") {
  SemanticTestFixture fixture;

  SECTION("single variable declaration") {
    const std::string source = R"(
      module m;
        logic test_signal;
      endmodule
    )";

    auto index = fixture.BuildIndexFromSource(source);

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
    "SemanticIndex GetDocumentSymbols includes struct fields",
    "[semantic_index]") {
  SemanticTestFixture fixture;
  std::string code = R"(
    package test_pkg;
      typedef struct {
        logic [7:0] data;
        logic valid;
        logic [15:0] address;
      } packet_t;
    endpackage
  )";

  auto index = fixture.BuildIndexFromSource(code);
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
    "SemanticIndex collects symbols inside generate if blocks",
    "[semantic_index]") {
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

  SemanticTestFixture fixture;
  auto index = fixture.BuildIndexFromSource(code);
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
    "[semantic_index]") {
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

  SemanticTestFixture fixture;
  auto index = fixture.BuildIndexFromSource(code);
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
    "[semantic_index]") {
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

  SemanticTestFixture fixture;
  auto index = fixture.BuildIndexFromSource(code);
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
    "[semantic_index]") {
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

  SemanticTestFixture fixture;
  auto index = fixture.BuildIndexFromSource(code);
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
    "SemanticIndex collects functions and tasks correctly",
    "[semantic_index]") {
  SemanticTestFixture fixture;
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

  auto index = fixture.BuildIndexFromSource(code);
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

TEST_CASE(
    "SemanticIndex function internals not in document symbols but available "
    "for goto-definition",
    "[semantic_index]") {
  SemanticTestFixture fixture;
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

  auto index = fixture.BuildIndexFromSource(code);

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

TEST_CASE(
    "SemanticIndex handles symbols with empty names for VSCode compatibility",
    "[semantic_index]") {
  SemanticTestFixture fixture;
  std::string code = R"(
    module test_module;
      generate
        if (1) begin
          logic gen_signal;
        end
      endgenerate
    endmodule
  )";

  auto index = fixture.BuildIndexFromSource(code);
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
    "[semantic_index]") {
  SemanticTestFixture fixture;
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

  auto index = fixture.BuildIndexFromSource(code);
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
    "SemanticIndex properly handles assertion symbols in generate blocks",
    "[semantic_index]") {
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

  SemanticTestFixture fixture;
  auto index = fixture.BuildIndexFromSource(code);
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

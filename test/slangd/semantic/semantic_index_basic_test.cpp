#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_all.hpp>

#include "slangd/semantic/semantic_index.hpp"
#include "test_fixtures.hpp"

auto main(int argc, char* argv[]) -> int {
  return Catch::Session().run(argc, argv);
}

namespace slangd::semantic {

using SemanticTestFixture = slangd::semantic::test::SemanticTestFixture;

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

  auto buffer = source_manager->assignText("test.sv", code);
  auto tree = slang::syntax::SyntaxTree::fromBuffer(buffer, *source_manager);
  if (tree) {
    compilation->addSyntaxTree(tree);
  }

  auto index = SemanticIndex::FromCompilation(*compilation, *source_manager);

  REQUIRE(index != nullptr);

  // Test LSP API: GetDocumentSymbols should return expected symbols
  auto document_symbols = index->GetDocumentSymbols("test.sv");
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
  auto document_symbols = index->GetDocumentSymbols("test.sv");
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
  auto symbols = index->GetDocumentSymbols("test.sv");

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

  auto buffer = source_manager->assignText("test.sv", code);
  auto tree = slang::syntax::SyntaxTree::fromBuffer(buffer, *source_manager);
  if (tree) {
    compilation->addSyntaxTree(tree);
  }

  auto index = SemanticIndex::FromCompilation(*compilation, *source_manager);

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

  auto buffer = source_manager->assignText("test.sv", code);
  auto tree = slang::syntax::SyntaxTree::fromBuffer(buffer, *source_manager);
  if (tree) {
    compilation->addSyntaxTree(tree);
  }

  auto index = SemanticIndex::FromCompilation(*compilation, *source_manager);

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

  // TODO(hankhsu): Add reference_map_ access methods to verify reference
  // tracking when GetReferenceMap() API is implemented
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

  auto buffer = source_manager->assignText("test.sv", code);
  auto tree = slang::syntax::SyntaxTree::fromBuffer(buffer, *source_manager);
  if (tree) {
    compilation->addSyntaxTree(tree);
  }

  auto index = SemanticIndex::FromCompilation(*compilation, *source_manager);

  // Test getter methods exist and return proper types
  const auto& definition_ranges = index->GetDefinitionRanges();
  const auto& reference_map = index->GetReferenceMap();

  // Basic sanity checks - should have some data
  REQUIRE(!definition_ranges.empty());

  // Verify reference_map is accessible (might be empty, that's OK)
  (void)reference_map;  // Suppress unused warning

  // Test GetDefinitionRange for some symbol
  if (!definition_ranges.empty()) {
    const auto& [first_key, first_range] = *definition_ranges.begin();
    auto retrieved_range = index->GetDefinitionRange(first_key);
    REQUIRE(retrieved_range.has_value());
    REQUIRE(retrieved_range.value() == first_range);
  }
}

TEST_CASE(
    "SemanticIndex LookupSymbolAt method exists and returns optional",
    "[semantic_index]") {
  std::string code = R"(
    module test_module;
      logic signal;
    endmodule
  )";

  auto source_manager = std::make_shared<slang::SourceManager>();
  slang::Bag options;
  auto compilation = std::make_unique<slang::ast::Compilation>(options);

  auto buffer = source_manager->assignText("test.sv", code);
  auto tree = slang::syntax::SyntaxTree::fromBuffer(buffer, *source_manager);
  if (tree) {
    compilation->addSyntaxTree(tree);
  }

  auto index = SemanticIndex::FromCompilation(*compilation, *source_manager);

  // Test that LookupSymbolAt exists and returns optional type
  // Using invalid location should return nullopt
  auto result = index->LookupSymbolAt(slang::SourceLocation());
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

    // Step 2: Add precise assertion using fixture helpers
    auto key = fixture.MakeKey(source, "test_signal");
    auto def_range = index->GetDefinitionRange(key);
    REQUIRE(def_range.has_value());
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
  auto symbols = index->GetDocumentSymbols("test.sv");

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
  auto symbols = index->GetDocumentSymbols("test.sv");

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
  auto symbols = index->GetDocumentSymbols("test.sv");

  // Find generate for loop block and verify it contains template symbols
  auto gen_loop = std::find_if(
      symbols[0].children->begin(), symbols[0].children->end(),
      [](const auto& s) { return s.name == "gen_loop"; });

  REQUIRE(gen_loop != symbols[0].children->end());
  REQUIRE(gen_loop->children.has_value());
  // Generate for loop should show template symbols once, not all iterations
  // Expected: i, loop_signal, LOOP_PARAM (3 unique symbols)
  REQUIRE(gen_loop->children->size() == 3);
}

}  // namespace slangd::semantic

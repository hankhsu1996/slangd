#include "slangd/semantic/semantic_index.hpp"

#include <memory>
#include <string>

#include <catch2/catch_all.hpp>
#include <slang/ast/Compilation.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/text/SourceManager.h>
#include <slang/util/Bag.h>

auto main(int argc, char* argv[]) -> int {
  return Catch::Session().run(argc, argv);
}

namespace slangd::semantic {

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
  // The preVisit hook should collect symbols from the root traversal
  REQUIRE(index->GetSymbolCount() > 0);

  // Verify we can access all symbols
  const auto& all_symbols = index->GetAllSymbols();
  REQUIRE(!all_symbols.empty());

  // Look for specific symbols we expect
  bool found_module = false;
  bool found_variable = false;
  for (const auto& [location, info] : all_symbols) {
    std::string name(info.symbol->name);
    if (name == "test_module") {
      found_module = true;
      REQUIRE(
          info.lsp_kind ==
          lsp::SymbolKind::kClass);  // Legacy maps modules to Class
    }
    if (name == "signal") {
      found_variable = true;
      REQUIRE(info.lsp_kind == lsp::SymbolKind::kVariable);
    }
  }

  REQUIRE(found_module);
  REQUIRE(found_variable);
}

TEST_CASE("SemanticIndex provides O(1) symbol lookup", "[semantic_index]") {
  std::string code = R"(
    package test_pkg;
      typedef logic [7:0] byte_t;
    endpackage

    module test_module;
      import test_pkg::*;
      byte_t data;
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

  // Find a symbol's location and verify O(1) lookup
  const auto& all_symbols = index->GetAllSymbols();

  slang::SourceLocation test_location;
  for (const auto& [location, info] : all_symbols) {
    if (std::string(info.symbol->name) == "test_pkg") {
      test_location = location;
      break;
    }
  }

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

  auto source_manager = std::make_shared<slang::SourceManager>();
  slang::Bag options;
  auto compilation = std::make_unique<slang::ast::Compilation>(options);

  auto buffer = source_manager->assignText("test.sv", code);
  auto tree = slang::syntax::SyntaxTree::fromBuffer(buffer, *source_manager);
  if (tree) {
    compilation->addSyntaxTree(tree);
  }

  auto index = SemanticIndex::FromCompilation(*compilation, *source_manager);

  // Count different symbol types we should find
  int interfaces = 0;
  int modules = 0;
  int enums = 0;
  int structs = 0;
  int modports = 0;
  int enum_values = 0;

  for (const auto& [location, info] : index->GetAllSymbols()) {
    switch (info.lsp_kind) {
      case lsp::SymbolKind::kInterface:
        if (std::string(info.symbol->name) == "master") {
          modports++;  // Modports mapped to Interface in legacy
        } else {
          interfaces++;  // Other interfaces
        }
        break;
      case lsp::SymbolKind::kClass:
        modules++;  // Modules mapped to Class in legacy
        break;
      case lsp::SymbolKind::kEnum:
        enums++;
        break;
      case lsp::SymbolKind::kStruct:
        structs++;
        break;
      case lsp::SymbolKind::kEnumMember:
        enum_values++;
        break;
      default:
        break;
    }
  }

  // Verify we found the expected symbols
  REQUIRE(interfaces >= 1);   // test_if
  REQUIRE(modules >= 1);      // test_module
  REQUIRE(enums >= 1);        // state_t enum
  REQUIRE(structs >= 1);      // packet_t struct
  REQUIRE(modports >= 1);     // master modport
  REQUIRE(enum_values >= 3);  // IDLE, ACTIVE, DONE

  // Verify total symbol count is substantial
  REQUIRE(index->GetSymbolCount() > 10);
}

TEST_CASE(
    "SemanticIndex GetDocumentSymbols with enum hierarchy",
    "[semantic_index]") {
  std::string code = R"(
    module test_module;
      typedef enum logic [1:0] {
        IDLE,
        ACTIVE,
        DONE
      } state_t;

      state_t state;
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

  // Test the new GetDocumentSymbols API
  auto document_symbols = index->GetDocumentSymbols("test.sv");

  REQUIRE(!document_symbols.empty());

  // Should find test_module as a root symbol
  bool found_module = false;
  for (const auto& symbol : document_symbols) {
    if (symbol.name == "test_module") {
      found_module = true;
      REQUIRE(symbol.kind == lsp::SymbolKind::kClass);

      // The module should have children
      REQUIRE(symbol.children.has_value());
      REQUIRE(!symbol.children->empty());

      // Look for state_t enum and its children
      bool found_enum = false;
      bool found_signal = false;
      bool found_state_var = false;

      for (const auto& child : *symbol.children) {
        if (child.name == "state_t") {
          found_enum = true;
          REQUIRE(child.kind == lsp::SymbolKind::kEnum);

          // The enum should have enum member children
          REQUIRE(child.children.has_value());
          REQUIRE(child.children->size() >= 3);  // IDLE, ACTIVE, DONE

          // Verify enum members
          int enum_members_found = 0;
          for (const auto& enum_child : *child.children) {
            if (enum_child.kind == lsp::SymbolKind::kEnumMember) {
              enum_members_found++;
            }
          }
          REQUIRE(enum_members_found >= 3);
        }
        if (child.name == "signal") {
          found_signal = true;
          REQUIRE(child.kind == lsp::SymbolKind::kVariable);
        }
        if (child.name == "state") {
          found_state_var = true;
          REQUIRE(child.kind == lsp::SymbolKind::kVariable);
        }
      }

      REQUIRE(found_enum);
      REQUIRE(found_signal);
      REQUIRE(found_state_var);
    }
  }

  REQUIRE(found_module);
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

}  // namespace slangd::semantic

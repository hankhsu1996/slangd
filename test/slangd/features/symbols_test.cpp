#include "include/slangd/features/symbols.hpp"

#include <string>

#include <asio.hpp>
#include <catch2/catch_all.hpp>
#include <slang/ast/Compilation.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/text/SourceManager.h>
#include <spdlog/spdlog.h>

#include "include/lsp/basic.hpp"
#include "include/lsp/document_features.hpp"

int main(int argc, char* argv[]) {
  spdlog::set_level(spdlog::level::debug);
  spdlog::set_pattern("[%l] %v");
  return Catch::Session().run(argc, argv);
}

// Helper function that combines compilation and symbol extraction
std::vector<lsp::DocumentSymbol> ExtractSymbolsFromString(
    const std::string& source) {
  const std::string filename = "test.sv";
  auto source_manager = std::make_shared<slang::SourceManager>();
  auto syntax_tree =
      slang::syntax::SyntaxTree::fromText(source, *source_manager, filename);
  auto compilation = std::make_unique<slang::ast::Compilation>();
  compilation->addSyntaxTree(syntax_tree);

  // Extract symbols
  std::string uri = "file://" + filename;
  return slangd::GetDocumentSymbols(*compilation, source_manager, uri);
}

TEST_CASE("GetDocumentSymbols extracts basic module", "[symbol_utils]") {
  std::string module_code = R"(
    module test_module;
    endmodule
  )";

  auto symbols = ExtractSymbolsFromString(module_code);

  REQUIRE(symbols.size() == 1);
  REQUIRE(symbols[0].kind == lsp::SymbolKind::Class);
}

TEST_CASE("GetDocumentSymbols extracts basic package", "[symbol_utils]") {
  // The simplest possible package
  std::string package_code = R"(
    package test_pkg;
    endpackage
  )";

  auto symbols = ExtractSymbolsFromString(package_code);

  REQUIRE(symbols.size() == 1);
  REQUIRE(symbols[0].kind == lsp::SymbolKind::Package);
}

TEST_CASE("GetDocumentSymbols extracts basic interface", "[symbol_utils]") {
  // The simplest possible interface
  std::string interface_code = R"(
    interface test_if;
    endinterface
  )";

  auto symbols = ExtractSymbolsFromString(interface_code);

  REQUIRE(symbols.size() == 1);
  REQUIRE(symbols[0].kind == lsp::SymbolKind::Interface);
}

TEST_CASE(
    "GetDocumentSymbols extracts module with parameters and variables",
    "[symbol_utils]") {
  // Module with parameters and variables
  std::string module_params_code = R"(
    module mod_with_param_and_var (
      parameter int WIDTH = 8;
      logic [WIDTH-1:0] data;
    endmodule
  )";

  auto symbols = ExtractSymbolsFromString(module_params_code);

  REQUIRE(symbols.size() == 1);
  REQUIRE(symbols[0].name == "mod_with_param_and_var");
  REQUIRE(symbols[0].kind == lsp::SymbolKind::Class);

  REQUIRE(symbols[0].children.has_value());
  REQUIRE(symbols[0].children->size() == 2);
  REQUIRE(symbols[0].children->at(0).name == "WIDTH");
  REQUIRE(symbols[0].children->at(0).kind == lsp::SymbolKind::Constant);
  REQUIRE(symbols[0].children->at(1).name == "data");
  REQUIRE(symbols[0].children->at(1).kind == lsp::SymbolKind::Variable);
}

TEST_CASE("GetDocumentSymbols extracts module ports", "[symbol_utils]") {
  // Module with ports
  std::string module_ports_code = R"(
    module mod_with_ports #(
      parameter WIDTH = 8
    )(
      input clk,
      output data
    );
    endmodule
  )";

  auto symbols = ExtractSymbolsFromString(module_ports_code);

  REQUIRE(symbols.size() == 1);
  REQUIRE(symbols[0].name == "mod_with_ports");
  REQUIRE(symbols[0].kind == lsp::SymbolKind::Class);

  REQUIRE(symbols[0].children->size() == 3);
  REQUIRE(symbols[0].children->at(0).name == "WIDTH");
  REQUIRE(symbols[0].children->at(0).kind == lsp::SymbolKind::Constant);
  REQUIRE(symbols[0].children->at(1).name == "clk");
  REQUIRE(symbols[0].children->at(1).kind == lsp::SymbolKind::Variable);
  REQUIRE(symbols[0].children->at(2).name == "data");
  REQUIRE(symbols[0].children->at(2).kind == lsp::SymbolKind::Variable);
}

TEST_CASE("GetDocumentSymbols extracts enum type", "[symbol_utils]") {
  // Package with enum
  // Note that in SystemVerilog, enum members are flattened into the parent
  // package scope.
  std::string enum_code = R"(
    package pkg_with_enum;
      typedef enum { RED, GREEN, BLUE } color_t;
    endpackage
  )";

  auto symbols = ExtractSymbolsFromString(enum_code);

  REQUIRE(symbols.size() == 1);
  REQUIRE(symbols[0].name == "pkg_with_enum");
  REQUIRE(symbols[0].kind == lsp::SymbolKind::Package);

  REQUIRE(symbols[0].children->size() == 4);
  REQUIRE(symbols[0].children->at(0).name == "RED");
  REQUIRE(symbols[0].children->at(0).kind == lsp::SymbolKind::Constant);
  REQUIRE(symbols[0].children->at(1).name == "GREEN");
  REQUIRE(symbols[0].children->at(1).kind == lsp::SymbolKind::Constant);
  REQUIRE(symbols[0].children->at(2).name == "BLUE");
  REQUIRE(symbols[0].children->at(2).kind == lsp::SymbolKind::Constant);
  REQUIRE(symbols[0].children->at(3).name == "color_t");
  REQUIRE(symbols[0].children->at(3).kind == lsp::SymbolKind::Enum);
}

TEST_CASE("GetDocumentSymbols extracts struct type", "[symbol_utils]") {
  // Package with struct
  std::string struct_code = R"(
    package pkg_with_struct;
      typedef struct {
        logic [7:0] a;
        logic [7:0] b;
      } my_struct_t;
    endpackage
  )";

  auto symbols = ExtractSymbolsFromString(struct_code);

  REQUIRE(symbols.size() == 1);
  REQUIRE(symbols[0].name == "pkg_with_struct");
  REQUIRE(symbols[0].kind == lsp::SymbolKind::Package);

  REQUIRE(symbols[0].children->size() == 1);
  REQUIRE(symbols[0].children->at(0).name == "my_struct_t");
  REQUIRE(symbols[0].children->at(0).kind == lsp::SymbolKind::Struct);

  REQUIRE(symbols[0].children->at(0).children->size() == 2);
  REQUIRE(symbols[0].children->at(0).children->at(0).name == "a");
  REQUIRE(
      symbols[0].children->at(0).children->at(0).kind ==
      lsp::SymbolKind::Field);
  REQUIRE(symbols[0].children->at(0).children->at(1).name == "b");
  REQUIRE(
      symbols[0].children->at(0).children->at(1).kind ==
      lsp::SymbolKind::Field);
}

TEST_CASE("GetDocumentSymbols extracts functions", "[symbol_utils]") {
  // Package with function
  std::string function_code = R"(
    package pkg_with_function;
      function int add(int a, int b);
        return a + b;
      endfunction
    endpackage
  )";

  auto symbols = ExtractSymbolsFromString(function_code);

  REQUIRE(symbols.size() == 1);
  REQUIRE(symbols[0].name == "pkg_with_function");
  REQUIRE(symbols[0].kind == lsp::SymbolKind::Package);

  REQUIRE(symbols[0].children->size() == 1);
  REQUIRE(symbols[0].children->at(0).name == "add");
  REQUIRE(symbols[0].children->at(0).kind == lsp::SymbolKind::Function);
}

TEST_CASE(
    "GetDocumentSymbols extracts multiple top-level symbols",
    "[symbol_utils]") {
  // Multiple top-level entities
  std::string multi_code = R"(
    module module1; endmodule
    module module2; endmodule
    package package1; endpackage
  )";

  auto symbols = ExtractSymbolsFromString(multi_code);

  REQUIRE(symbols.size() == 3);
  REQUIRE(symbols[0].name == "module1");
  REQUIRE(symbols[0].kind == lsp::SymbolKind::Class);
  REQUIRE(symbols[1].name == "module2");
  REQUIRE(symbols[1].kind == lsp::SymbolKind::Class);
  REQUIRE(symbols[2].name == "package1");
  REQUIRE(symbols[2].kind == lsp::SymbolKind::Package);
}

TEST_CASE("GetDocumentSymbols extracts nested struct", "[symbol_utils]") {
  // Package with nested struct
  std::string nested_struct_code = R"(
    package pkg_with_nested_struct;
      typedef struct {
        struct {
          logic [7:0] a;
        } inner;
      } my_struct_t;
    endpackage
  )";

  auto symbols = ExtractSymbolsFromString(nested_struct_code);

  REQUIRE(symbols.size() == 1);
  REQUIRE(symbols[0].name == "pkg_with_nested_struct");
  REQUIRE(symbols[0].kind == lsp::SymbolKind::Package);

  REQUIRE(symbols[0].children->size() == 1);
  REQUIRE(symbols[0].children->at(0).name == "my_struct_t");
  REQUIRE(symbols[0].children->at(0).kind == lsp::SymbolKind::Struct);

  REQUIRE(symbols[0].children->at(0).children->size() == 1);
  REQUIRE(symbols[0].children->at(0).children->at(0).name == "inner");
  REQUIRE(
      symbols[0].children->at(0).children->at(0).kind ==
      lsp::SymbolKind::Field);

  REQUIRE(symbols[0].children->at(0).children->at(0).children->size() == 1);
  REQUIRE(
      symbols[0].children->at(0).children->at(0).children->at(0).name == "a");
  REQUIRE(
      symbols[0].children->at(0).children->at(0).children->at(0).kind ==
      lsp::SymbolKind::Field);
}

TEST_CASE("GetDocumentSymbols extracts type parameters", "[symbol_utils]") {
  std::string type_param_code = R"(
    module mod_with_type_param #(
      parameter type T = logic [7:0]
    )(
      input T data,
      output T data_out
    );
    endmodule
  )";

  auto symbols = ExtractSymbolsFromString(type_param_code);

  REQUIRE(symbols.size() == 1);
  REQUIRE(symbols[0].name == "mod_with_type_param");
  REQUIRE(symbols[0].kind == lsp::SymbolKind::Class);

  REQUIRE(symbols[0].children->size() == 3);
  REQUIRE(symbols[0].children->at(0).name == "T");
  REQUIRE(symbols[0].children->at(0).kind == lsp::SymbolKind::TypeParameter);
  REQUIRE(symbols[0].children->at(1).name == "data");
  REQUIRE(symbols[0].children->at(1).kind == lsp::SymbolKind::Variable);
  REQUIRE(symbols[0].children->at(2).name == "data_out");
  REQUIRE(symbols[0].children->at(2).kind == lsp::SymbolKind::Variable);
}

TEST_CASE(
    "GetDocumentSymbols extracts module instantiation", "[symbol_utils]") {
  std::string module_inst_code = R"(
    module submodule;
      logic [7:0] a;
    endmodule

    module mod_with_inst;
      submodule submod();
    endmodule
  )";

  auto symbols = ExtractSymbolsFromString(module_inst_code);

  REQUIRE(symbols.size() == 2);
  REQUIRE(symbols[0].name == "mod_with_inst");
  REQUIRE(symbols[0].kind == lsp::SymbolKind::Class);
  REQUIRE(symbols[1].name == "submodule");
  REQUIRE(symbols[1].kind == lsp::SymbolKind::Class);

  REQUIRE(symbols[0].children->size() == 1);
  REQUIRE(symbols[0].children->at(0).name == "submod");
  REQUIRE(symbols[0].children->at(0).kind == lsp::SymbolKind::Variable);

  REQUIRE(symbols[1].children->size() == 1);
  REQUIRE(symbols[1].children->at(0).name == "a");
  REQUIRE(symbols[1].children->at(0).kind == lsp::SymbolKind::Variable);
}

TEST_CASE(
    "GetDocumentSymbols extracts unknown module instantiation",
    "[symbol_utils]") {
  std::string module_inst_code = R"(
    module mod_with_inst;
      submodule submod();
    endmodule
  )";

  auto symbols = ExtractSymbolsFromString(module_inst_code);

  REQUIRE(symbols.size() == 1);
  REQUIRE(symbols[0].name == "mod_with_inst");
  REQUIRE(symbols[0].kind == lsp::SymbolKind::Class);

  REQUIRE(symbols[0].children->size() == 1);
  REQUIRE(symbols[0].children->at(0).name == "submod");
  REQUIRE(symbols[0].children->at(0).kind == lsp::SymbolKind::Variable);
}

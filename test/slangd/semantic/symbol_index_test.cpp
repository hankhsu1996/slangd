#include "slangd/semantic/symbol_index.hpp"

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

// Helper function to extract symbols from string (similar to legacy test)
auto ExtractSymbolsFromString(const std::string& source)
    -> std::vector<lsp::DocumentSymbol> {
  // Create source manager and compilation
  auto source_manager = std::make_shared<slang::SourceManager>();
  slang::Bag options;
  auto compilation = std::make_unique<slang::ast::Compilation>(options);

  // Add source
  auto buffer = source_manager->assignText("test.sv", source);
  auto tree = slang::syntax::SyntaxTree::fromBuffer(buffer, *source_manager);
  if (tree) {
    compilation->addSyntaxTree(tree);
  }

  // Create symbol index and extract symbols
  auto index = SymbolIndex::FromCompilation(*compilation, *source_manager);
  return index->GetDocumentSymbols("test.sv");
}

TEST_CASE("SymbolIndex extracts basic module", "[symbol_index]") {
  std::string module_code = R"(
    module test_module;
    endmodule
  )";

  auto symbols = ExtractSymbolsFromString(module_code);

  REQUIRE(symbols.size() == 1);
  REQUIRE(symbols[0].name == "test_module");
  REQUIRE(symbols[0].kind == lsp::SymbolKind::kClass);
}

TEST_CASE("SymbolIndex extracts basic package", "[symbol_index]") {
  std::string package_code = R"(
    package test_pkg;
    endpackage
  )";

  auto symbols = ExtractSymbolsFromString(package_code);

  REQUIRE(symbols.size() == 1);
  REQUIRE(symbols[0].name == "test_pkg");
  REQUIRE(symbols[0].kind == lsp::SymbolKind::kPackage);
}

TEST_CASE("SymbolIndex extracts multiple top-level symbols", "[symbol_index]") {
  std::string multi_code = R"(
    module module1; endmodule
    module module2; endmodule
    package package1; endpackage
  )";

  auto symbols = ExtractSymbolsFromString(multi_code);

  REQUIRE(symbols.size() == 3);
  REQUIRE(symbols[0].name == "module1");
  REQUIRE(symbols[0].kind == lsp::SymbolKind::kClass);
  REQUIRE(symbols[1].name == "module2");
  REQUIRE(symbols[1].kind == lsp::SymbolKind::kClass);
  REQUIRE(symbols[2].name == "package1");
  REQUIRE(symbols[2].kind == lsp::SymbolKind::kPackage);
}

}  // namespace slangd::semantic

#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_all.hpp>
#include <lsp/document_features.hpp>
#include <slang/syntax/SyntaxTree.h>
#include <slang/text/SourceManager.h>

#include "slangd/syntax/syntax_document_symbol_visitor.hpp"
#include "slangd/utils/compilation_options.hpp"

namespace slangd::test {

struct SyntaxDocumentSymbolResult {
  std::vector<lsp::DocumentSymbol> symbols;
  std::string uri;
};

class SyntaxDocumentSymbolFixture {
 public:
  static auto BuildSymbols(std::string_view code)
      -> SyntaxDocumentSymbolResult {
    auto source_manager = std::make_shared<slang::SourceManager>();
    auto options = slangd::utils::CreateLspCompilationOptions();

    auto buffer = source_manager->assignText("test.sv", std::string(code));
    auto syntax_tree =
        slang::syntax::SyntaxTree::fromBuffer(buffer, *source_manager, options);

    REQUIRE(syntax_tree);

    slangd::syntax::SyntaxDocumentSymbolVisitor visitor(
        "file:///test.sv", *source_manager, buffer.id);
    syntax_tree->root().visit(visitor);

    return {.symbols = visitor.GetResult(), .uri = "file:///test.sv"};
  }

  static auto AssertSymbolExists(
      const SyntaxDocumentSymbolResult& result, const std::string& name,
      lsp::SymbolKind kind) -> void {
    const auto* symbol = FindSymbol(result.symbols, name);
    REQUIRE(symbol != nullptr);
    REQUIRE(symbol->kind == kind);
  }

 private:
  static auto FindSymbol(
      const std::vector<lsp::DocumentSymbol>& symbols, const std::string& name)
      -> const lsp::DocumentSymbol* {
    for (const auto& symbol : symbols) {
      if (symbol.name == name) {
        return &symbol;
      }
      if (symbol.children) {
        const auto* found = FindSymbol(*symbol.children, name);
        if (found != nullptr) {
          return found;
        }
      }
    }
    return nullptr;
  }
};

}  // namespace slangd::test

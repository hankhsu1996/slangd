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

  static auto AssertSymbol(
      const SyntaxDocumentSymbolResult& result,
      const std::vector<std::string>& path, lsp::SymbolKind kind) -> void {
    REQUIRE(!path.empty());

    const std::vector<lsp::DocumentSymbol>* current_level = &result.symbols;

    for (size_t i = 0; i < path.size(); ++i) {
      const auto& name = path[i];
      const lsp::DocumentSymbol* found = nullptr;

      for (const auto& symbol : *current_level) {
        if (symbol.name == name) {
          found = &symbol;
          break;
        }
      }

      REQUIRE(found != nullptr);

      if (i == path.size() - 1) {
        REQUIRE(found->kind == kind);
      } else {
        REQUIRE(found->children.has_value());
        current_level = &(*found->children);
      }
    }
  }
};

}  // namespace slangd::test

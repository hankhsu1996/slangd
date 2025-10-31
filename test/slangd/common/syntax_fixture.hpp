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
  std::string source;
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

    return {
        .symbols = visitor.GetResult(),
        .uri = "file:///test.sv",
        .source = std::string(code)};
  }

  static auto FindSymbol(
      const SyntaxDocumentSymbolResult& result,
      const std::vector<std::string>& path) -> const lsp::DocumentSymbol* {
    if (path.empty()) {
      return nullptr;
    }

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

      if (found == nullptr) {
        return nullptr;
      }

      if (i == path.size() - 1) {
        return found;
      }

      if (!found->children.has_value()) {
        return nullptr;
      }
      current_level = &(*found->children);
    }

    return nullptr;
  }

  // Convert LSP position to byte offset (ASCII-only for tests)
  static auto ConvertLspPositionToOffset(
      const std::string& source, const lsp::Position& pos) -> size_t {
    int current_line = 0;
    size_t line_start = 0;

    for (size_t i = 0; i < source.size(); ++i) {
      if (current_line == pos.line) {
        return line_start + pos.character;
      }
      if (source[i] == '\n') {
        current_line++;
        line_start = i + 1;
      }
    }

    // If we reach here, pos is at or beyond last line
    return line_start + pos.character;
  }

  // Extract text from source using LSP range
  static auto ExtractRangeText(
      const std::string& source, const lsp::Range& range) -> std::string {
    size_t start_offset = ConvertLspPositionToOffset(source, range.start);
    size_t end_offset = ConvertLspPositionToOffset(source, range.end);
    return source.substr(start_offset, end_offset - start_offset);
  }

  static auto AssertSymbol(
      const SyntaxDocumentSymbolResult& result,
      const std::vector<std::string>& path, lsp::SymbolKind kind) -> void {
    REQUIRE(!path.empty());
    const auto* symbol = FindSymbol(result, path);
    REQUIRE(symbol != nullptr);
    REQUIRE(symbol->kind == kind);

    // Always verify range is just the symbol name (not the entire body)
    const std::string& expected_name = path.back();
    std::string actual_text = ExtractRangeText(result.source, symbol->range);
    REQUIRE(actual_text == expected_name);
  }

  static auto AssertSymbolChildCount(
      const SyntaxDocumentSymbolResult& result,
      const std::vector<std::string>& path, size_t expected_count) -> void {
    REQUIRE(!path.empty());
    const auto* symbol = FindSymbol(result, path);
    REQUIRE(symbol != nullptr);
    REQUIRE(symbol->children.has_value());
    REQUIRE(symbol->children->size() == expected_count);
  }
};

}  // namespace slangd::test

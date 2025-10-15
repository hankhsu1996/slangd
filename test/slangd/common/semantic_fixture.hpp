#pragma once

#include <memory>
#include <regex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>
#include <lsp/basic.hpp>
#include <slang/ast/Compilation.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/text/SourceManager.h>
#include <slang/util/Bag.h>

#include "slangd/semantic/semantic_index.hpp"

namespace slangd::test {

// Base fixture for all semantic index tests
class SemanticTestFixture {
 public:
  using SemanticIndex = slangd::semantic::SemanticIndex;

  // Result struct that bundles index with its dependencies
  // Keeps source_manager and compilation alive (index stores pointers to them)
  struct IndexWithDependencies {
    std::unique_ptr<SemanticIndex> index;
    std::shared_ptr<slang::SourceManager> source_manager;
    std::unique_ptr<slang::ast::Compilation> compilation;
    std::string uri;
  };

  auto static BuildIndexFromSource(const std::string& source)
      -> IndexWithDependencies {
    constexpr std::string_view kTestFilename = "test.sv";

    // Use consistent URI/path format
    std::string test_uri = "file:///" + std::string(kTestFilename);
    std::string test_path = "/" + std::string(kTestFilename);

    auto source_manager = std::make_shared<slang::SourceManager>();
    auto buffer = source_manager->assignText(test_path, source);
    auto tree = slang::syntax::SyntaxTree::fromBuffer(buffer, *source_manager);

    slang::Bag options;
    auto compilation = std::make_unique<slang::ast::Compilation>(options);
    compilation->addSyntaxTree(tree);

    auto index =
        SemanticIndex::FromCompilation(*compilation, *source_manager, test_uri);

    return IndexWithDependencies{
        .index = std::move(index),
        .source_manager = std::move(source_manager),
        .compilation = std::move(compilation),
        .uri = std::move(test_uri)};
  }

  // Simple helper: convert byte offset to LSP position (ASCII-only for tests)
  static auto ConvertOffsetToLspPosition(
      const std::string& source, size_t offset) -> lsp::Position {
    int line = 0;
    size_t line_start = 0;

    for (size_t i = 0; i < offset; i++) {
      if (source[i] == '\n') {
        line++;
        line_start = i + 1;
      }
    }

    auto character = static_cast<int>(offset - line_start);
    return lsp::Position{.line = line, .character = character};
  }

  // Find position of text in source (LSP coordinates)
  // Simple ASCII-only conversion suitable for test code
  static auto FindLocation(const std::string& source, const std::string& text)
      -> lsp::Position {
    size_t offset = source.find(text);
    if (offset == std::string::npos) {
      throw std::runtime_error(
          fmt::format("FindLocation: Text '{}' not found in source", text));
    }

    return ConvertOffsetToLspPosition(source, offset);
  }

  // Find all LSP positions of a symbol in source code
  static auto FindAllOccurrences(
      const std::string& code, const std::string& symbol_name)
      -> std::vector<lsp::Position> {
    std::vector<lsp::Position> positions;
    std::string pattern = R"(\b)" + std::string(symbol_name) + R"(\b)";
    std::regex symbol_regex(pattern);

    auto begin = std::sregex_iterator(code.begin(), code.end(), symbol_regex);
    auto end = std::sregex_iterator();

    for (auto it = begin; it != end; ++it) {
      auto offset = static_cast<size_t>(it->position());
      positions.push_back(ConvertOffsetToLspPosition(code, offset));
    }

    if (positions.empty()) {
      throw std::runtime_error(
          fmt::format(
              "FindAllOccurrences: No occurrences of '{}' found", symbol_name));
    }

    return positions;
  }
};

}  // namespace slangd::test

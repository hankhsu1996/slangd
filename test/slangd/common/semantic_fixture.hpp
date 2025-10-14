#pragma once

#include <memory>
#include <regex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_all.hpp>
#include <fmt/format.h>
#include <slang/ast/Compilation.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/text/SourceLocation.h>
#include <slang/text/SourceManager.h>
#include <slang/util/Bag.h>

#include "slangd/semantic/semantic_index.hpp"

namespace slangd::semantic::test {

// Base fixture for all semantic index tests
class SemanticTestFixture {
 public:
  using SemanticIndex = slangd::semantic::SemanticIndex;
  using SymbolKey = slangd::semantic::SymbolKey;
  auto BuildIndexFromSource(const std::string& source)
      -> std::unique_ptr<SemanticIndex> {
    constexpr std::string_view kTestFilename = "test.sv";

    // Use consistent URI/path format
    std::string test_uri = "file:///" + std::string(kTestFilename);
    std::string test_path = "/" + std::string(kTestFilename);

    SetSourceManager(std::make_shared<slang::SourceManager>());
    auto buffer = GetSourceManager()->assignText(test_path, source);
    SetBufferId(buffer.id);
    auto tree =
        slang::syntax::SyntaxTree::fromBuffer(buffer, *GetSourceManager());

    slang::Bag options;
    SetCompilation(std::make_unique<slang::ast::Compilation>(options));
    GetCompilation()->addSyntaxTree(tree);

    return SemanticIndex::FromCompilation(
        *GetCompilation(), *GetSourceManager(), test_uri);
  }

  auto MakeKey(const std::string& source, const std::string& symbol)
      -> SymbolKey {
    size_t offset = source.find(symbol);

    if (offset == std::string::npos) {
      throw std::runtime_error(
          fmt::format("MakeKey: Symbol '{}' not found in source", symbol));
    }

    // Detect ambiguous symbol names early
    size_t second_occurrence = source.find(symbol, offset + 1);
    if (second_occurrence != std::string::npos) {
      throw std::runtime_error(
          fmt::format(
              "MakeKey: Ambiguous symbol '{}' found at multiple locations. "
              "Use unique descriptive names (e.g., 'test_signal' not 'signal') "
              "or use MakeKeyAt({}) for specific occurrence.",
              symbol, offset));
    }

    return SymbolKey{.bufferId = buffer_id_.getId(), .offset = offset};
  }

  // Alternative method for cases where multiple occurrences are expected
  auto MakeKeyAt(
      const std::string& source, const std::string& symbol,
      size_t occurrence = 0) -> SymbolKey {
    size_t offset = 0;
    for (size_t i = 0; i <= occurrence; ++i) {
      offset = source.find(symbol, offset);
      if (offset == std::string::npos) {
        throw std::runtime_error(
            fmt::format(
                "MakeKeyAt: Symbol '{}' occurrence {} not found in source",
                symbol, occurrence));
      }
      if (i < occurrence) {
        offset += symbol.length();
      }
    }
    return SymbolKey{.bufferId = buffer_id_.getId(), .offset = offset};
  }

  auto MakeRange(
      const std::string& source, const std::string& search_string,
      size_t symbol_size) -> slang::SourceRange {
    size_t offset = source.find(search_string);
    auto start = slang::SourceLocation{buffer_id_, offset};
    auto end = slang::SourceLocation{buffer_id_, offset + symbol_size};
    return slang::SourceRange{start, end};
  }

  auto FindLocation(const std::string& source, const std::string& text)
      -> slang::SourceLocation {
    size_t offset = source.find(text);
    if (offset == std::string::npos) {
      return {};
    }
    return slang::SourceLocation{buffer_id_, offset};
  }

  static auto FindSymbolOffsetsInText(
      const std::string& text, std::string_view symbol_name)
      -> std::vector<size_t> {
    std::vector<size_t> offsets;
    std::string pattern = R"(\b)" + std::string(symbol_name) + R"(\b)";
    std::regex symbol_regex(pattern);

    auto begin = std::sregex_iterator(text.begin(), text.end(), symbol_regex);
    auto end = std::sregex_iterator();

    for (auto it = begin; it != end; ++it) {
      offsets.push_back(static_cast<size_t>(it->position()));
    }

    return offsets;
  }

  auto FindAllOccurrences(
      const std::string& code, const std::string& symbol_name)
      -> std::vector<slang::SourceLocation> {
    auto offsets = FindSymbolOffsetsInText(code, symbol_name);

    if (offsets.empty()) {
      throw std::runtime_error(
          fmt::format(
              "FindAllOccurrences: No occurrences of '{}' found", symbol_name));
    }

    std::vector<slang::SourceLocation> occurrences;
    for (size_t offset : offsets) {
      occurrences.emplace_back(buffer_id_, offset);
    }

    return occurrences;
  }

  // Public accessors for derived classes and tests
  [[nodiscard]] auto GetBufferId() const -> uint32_t {
    return buffer_id_.getId();
  }
  [[nodiscard]] auto GetSourceManager() const -> slang::SourceManager* {
    return source_manager_.get();
  }
  [[nodiscard]] auto GetCompilation() const -> slang::ast::Compilation* {
    return compilation_.get();
  }

 protected:
  // Protected setters for derived classes to modify state
  void SetSourceManager(std::shared_ptr<slang::SourceManager> sm) {
    source_manager_ = std::move(sm);
  }
  void SetCompilation(std::unique_ptr<slang::ast::Compilation> comp) {
    compilation_ = std::move(comp);
  }
  void SetBufferId(slang::BufferID id) {
    buffer_id_ = id;
  }

 private:
  std::shared_ptr<slang::SourceManager> source_manager_;
  std::unique_ptr<slang::ast::Compilation> compilation_;
  slang::BufferID buffer_id_;
};

}  // namespace slangd::semantic::test

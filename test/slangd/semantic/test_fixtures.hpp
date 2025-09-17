#pragma once

#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <asio.hpp>
#include <fmt/format.h>
#include <slang/ast/Compilation.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/text/SourceLocation.h>
#include <slang/text/SourceManager.h>
#include <slang/util/Bag.h>

#include "slangd/semantic/semantic_index.hpp"
#include "slangd/utils/canonical_path.hpp"

namespace slangd::semantic::test {

// Base fixture for all semantic index tests
class SemanticTestFixture {
 public:
  using SemanticIndex = slangd::semantic::SemanticIndex;
  using SymbolKey = slangd::semantic::SymbolKey;
  auto BuildIndexFromSource(const std::string& source)
      -> std::unique_ptr<SemanticIndex> {
    std::string path = "test.sv";
    SetSourceManager(std::make_shared<slang::SourceManager>());
    auto buffer = GetSourceManager()->assignText(path, source);
    SetBufferId(buffer.id);
    auto tree =
        slang::syntax::SyntaxTree::fromBuffer(buffer, *GetSourceManager());

    slang::Bag options;
    SetCompilation(std::make_unique<slang::ast::Compilation>(options));
    GetCompilation()->addSyntaxTree(tree);

    return SemanticIndex::FromCompilation(
        *GetCompilation(), *GetSourceManager());
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
      throw std::runtime_error(fmt::format(
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
        throw std::runtime_error(fmt::format(
            "MakeKeyAt: Symbol '{}' occurrence {} not found in source", symbol,
            occurrence));
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

// Extended fixture for multifile tests
class MultiFileSemanticFixture : public SemanticTestFixture {
 public:
  MultiFileSemanticFixture() {
    // Create a temporary directory for test files
    temp_dir_ =
        std::filesystem::temp_directory_path() / "slangd_semantic_multifile";
    std::filesystem::create_directories(temp_dir_);
  }

  ~MultiFileSemanticFixture() {
    // Clean up test files
    std::error_code ec;
    std::filesystem::remove_all(temp_dir_, ec);
  }

  // Explicitly delete copy operations
  MultiFileSemanticFixture(const MultiFileSemanticFixture&) = delete;
  auto operator=(const MultiFileSemanticFixture&)
      -> MultiFileSemanticFixture& = delete;

  // Explicitly delete move operations
  MultiFileSemanticFixture(MultiFileSemanticFixture&&) = delete;
  auto operator=(MultiFileSemanticFixture&&)
      -> MultiFileSemanticFixture& = delete;

  [[nodiscard]] auto GetTempDir() const -> slangd::CanonicalPath {
    return slangd::CanonicalPath(temp_dir_);
  }

  auto CreateFile(std::string_view filename, std::string_view content)
      -> slangd::CanonicalPath {
    auto file_path = temp_dir_ / filename;
    std::ofstream file(file_path);
    file << content;
    file.close();
    return slangd::CanonicalPath(file_path);
  }

  // Build index from multiple files
  auto BuildIndexFromFiles(const std::vector<std::string>& file_contents)
      -> std::unique_ptr<SemanticIndex> {
    SetSourceManager(std::make_shared<slang::SourceManager>());
    slang::Bag options;
    SetCompilation(std::make_unique<slang::ast::Compilation>(options));

    // Add each file to the compilation
    for (size_t i = 0; i < file_contents.size(); ++i) {
      std::string filename = fmt::format("file_{}.sv", i);
      auto buffer = GetSourceManager()->assignText(filename, file_contents[i]);
      auto tree =
          slang::syntax::SyntaxTree::fromBuffer(buffer, *GetSourceManager());
      GetCompilation()->addSyntaxTree(tree);

      // Store the first buffer ID for key creation
      if (i == 0) {
        SetBufferId(buffer.id);
      }
    }

    return SemanticIndex::FromCompilation(
        *GetCompilation(), *GetSourceManager());
  }

 private:
  std::filesystem::path temp_dir_;
};

// Async test runner for coroutine tests
template <typename F>
void RunAsyncTest(F&& test_fn) {
  asio::io_context io_context;
  auto executor = io_context.get_executor();

  bool completed = false;
  std::exception_ptr exception;

  asio::co_spawn(
      io_context,
      [fn = std::forward<F>(test_fn), &completed, &exception,
       executor]() -> asio::awaitable<void> {
        try {
          co_await fn(executor);
          completed = true;
        } catch (...) {
          exception = std::current_exception();
          completed = true;
        }
      },
      asio::detached);

  io_context.run();

  if (exception) {
    std::rethrow_exception(exception);
  }

  // Note: REQUIRE macro should be called by the caller if needed
}

}  // namespace slangd::semantic::test

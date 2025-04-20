#include "slangd/semantic/symbol_index.hpp"

#include <memory>
#include <string>

#include <catch2/catch_all.hpp>
#include <spdlog/spdlog.h>

#include "slang/ast/Compilation.h"
#include "slang/syntax/SyntaxTree.h"
#include "slang/text/SourceManager.h"

auto main(int argc, char* argv[]) -> int {
  spdlog::set_level(spdlog::level::debug);
  spdlog::set_pattern("[%l] %v");
  return Catch::Session().run(argc, argv);
}

class SymbolIndexFixture {
  using SymbolIndex = slangd::semantic::SymbolIndex;
  using SymbolKey = slangd::semantic::SymbolKey;

 public:
  auto BuildIndexFromSource(const std::string& source) -> SymbolIndex {
    std::string path = "test.sv";
    sourceManager_ = std::make_unique<slang::SourceManager>();
    auto buffer = sourceManager_->assignText(path, source);
    buffer_id_ = buffer.id;
    auto tree = slang::syntax::SyntaxTree::fromBuffer(buffer, *sourceManager_);

    compilation_ = std::make_unique<slang::ast::Compilation>();
    compilation_->addSyntaxTree(tree);

    return SymbolIndex::FromCompilation(*compilation_, {path});
  }

  auto MakeKey(const std::string& source, const std::string& symbol)
      -> SymbolKey {
    size_t offset = source.find(symbol);
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

  [[nodiscard]] auto GetBufferId() const -> uint32_t {
    return buffer_id_.getId();
  }
  [[nodiscard]] auto GetSourceManager() const -> slang::SourceManager* {
    return sourceManager_.get();
  }

 private:
  std::unique_ptr<slang::SourceManager> sourceManager_;
  std::unique_ptr<slang::ast::Compilation> compilation_;
  slang::BufferID buffer_id_;
};

TEST_CASE("SymbolIndex definition tracking", "[symbol_index]") {
  SymbolIndexFixture fixture;

  SECTION("basic logic declaration") {
    const std::string source = R"(
      module m;
        logic test_signal;
      endmodule
    )";

    auto index = fixture.BuildIndexFromSource(source);
    REQUIRE(!index.GetDefinitionRanges().empty());
    REQUIRE(index.GetDefinitionRanges().contains(
        fixture.MakeKey(source, "test_signal")));
  }

  SECTION("nested scope indexing") {
    const std::string source = R"(
      module m;
        if (1) begin
          logic nested_signal;
        end
      endmodule
    )";

    auto index = fixture.BuildIndexFromSource(source);
    REQUIRE(!index.GetDefinitionRanges().empty());
    REQUIRE(index.GetDefinitionRanges().contains(
        fixture.MakeKey(source, "nested_signal")));
  }

  SECTION("multiple symbols indexing") {
    const std::string source = R"(
      module m;
        logic test_signal_1, test_signal_2, test_signal_3;
      endmodule
    )";

    auto index = fixture.BuildIndexFromSource(source);
    REQUIRE(!index.GetDefinitionRanges().empty());

    for (const std::string& name :
         {"test_signal_1", "test_signal_2", "test_signal_3"}) {
      REQUIRE(
          index.GetDefinitionRanges().contains(fixture.MakeKey(source, name)));
    }
  }
}

TEST_CASE("SymbolIndex reference tracking", "[symbol_index]") {
  SymbolIndexFixture fixture;

  const std::string source = R"(
    module m;
      logic test_signal;

      initial begin
        test_signal = 1; // Reference to test_signal
      end
    endmodule
  )";

  auto index = fixture.BuildIndexFromSource(source);
  auto ref_map = index.GetReferenceMap();
  auto def_key = fixture.MakeKey(source, "test_signal");
  auto ref_range = fixture.MakeRange(
      source, "test_signal = 1", std::string("test_signal").size());
  REQUIRE(ref_map.contains(ref_range));
  REQUIRE(ref_map.at(ref_range) == def_key);
}

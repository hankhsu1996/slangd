#include "slangd/semantic/definition_index.hpp"

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

class DefinitionIndexFixture {
  using DefinitionIndex = slangd::semantic::DefinitionIndex;
  using SymbolKey = slangd::semantic::SymbolKey;

 public:
  auto BuildIndexFromSource(const std::string& source) -> DefinitionIndex {
    std::string path = "test.sv";
    sourceManager_ = std::make_unique<slang::SourceManager>();
    auto buffer = sourceManager_->assignText(path, source);
    buffer_id_ = buffer.id;
    auto tree = slang::syntax::SyntaxTree::fromBuffer(buffer, *sourceManager_);

    compilation_ = std::make_unique<slang::ast::Compilation>();
    compilation_->addSyntaxTree(tree);

    return DefinitionIndex::FromCompilation(*compilation_, {buffer_id_});
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

TEST_CASE("DefinitionIndex definition tracking", "[symbol_index]") {
  DefinitionIndexFixture fixture;

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

TEST_CASE("DefinitionIndex reference tracking", "[symbol_index]") {
  DefinitionIndexFixture fixture;

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

TEST_CASE(
    "DefinitionIndex handles interface ports without crash", "[symbol_index]") {
  DefinitionIndexFixture fixture;

  SECTION("basic interface port with member access") {
    const std::string source = R"(
      interface cpu_if;
        logic [31:0] addr;
        logic [31:0] data;
      endinterface

      module cpu_core(cpu_if.master bus);
        assign bus.addr = 32'h1000;
        assign bus.data = 32'hDEAD;
        logic internal_var;
      endmodule
    )";

    // Primary goal: This should not crash during symbol indexing
    auto index = fixture.BuildIndexFromSource(source);

    // Secondary goal: Basic sanity check that indexing still works
    REQUIRE(!index.GetDefinitionRanges().empty());

    // Just verify that SOME symbols are indexed (crash prevention is the main
    // goal) Interface definitions may not be indexed the same way as variables
    REQUIRE(index.GetDefinitionRanges().contains(
        fixture.MakeKey(source, "internal_var")));
  }

  SECTION("undefined interface - single file resilience") {
    const std::string source = R"(
      // No interface definition - testing LSP resilience
      module processor(undefined_if bus);
        assign bus.signal = 1'b1;    // Interface doesn't exist
        assign bus.data = 32'hDEAD;  // Member doesn't exist

        // Regular symbols should still be indexed
        logic internal_state;
        logic [7:0] counter;
      endmodule
    )";

    // Primary: Should not crash even with undefined interface
    auto index = fixture.BuildIndexFromSource(source);

    // Secondary: Regular symbols still indexed despite interface errors
    REQUIRE(!index.GetDefinitionRanges().empty());
    REQUIRE(index.GetDefinitionRanges().contains(
        fixture.MakeKey(source, "internal_state")));
    REQUIRE(index.GetDefinitionRanges().contains(
        fixture.MakeKey(source, "counter")));

    // The undefined interface references (bus.signal, bus.data) are gracefully
    // handled
  }

  SECTION("interface in always_comb conditions and RHS") {
    const std::string source = R"(
      // Pattern that triggers Expression::tryBindInterfaceRef in procedural blocks
      module generic_module(generic_if iface);
        logic state;
        logic [7:0] counter;
        logic enable;

        always_comb begin
          if (enable & ~iface.ready) begin      // Interface in condition
            state = 1'b0;
          end else if (enable & iface.ready) begin
            if (iface.mode == 1'b1) begin      // Interface in comparison
              state = 1'b1;
            end else begin
              counter = iface.data;            // Interface in RHS assignment
            end
          end
        end
      endmodule
    )";

    // Primary: Should not crash with interface expressions in always_comb
    auto index = fixture.BuildIndexFromSource(source);

    // Secondary: Regular symbols still indexed despite interface usage
    REQUIRE(!index.GetDefinitionRanges().empty());
    REQUIRE(
        index.GetDefinitionRanges().contains(fixture.MakeKey(source, "state")));
    REQUIRE(index.GetDefinitionRanges().contains(
        fixture.MakeKey(source, "counter")));
    REQUIRE(index.GetDefinitionRanges().contains(
        fixture.MakeKey(source, "enable")));

    // This test targets Expression::tryBindInterfaceRef code path
    // that differs from simple continuous assignments
  }
}

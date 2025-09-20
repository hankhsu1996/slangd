#include "slangd/semantic/symbol_utils.hpp"

#include <cstdlib>
#include <memory>
#include <string>

#include <catch2/catch_all.hpp>
#include <slang/ast/Compilation.h>
#include <slang/ast/Symbol.h>
#include <slang/syntax/SyntaxTree.h>
#include <spdlog/spdlog.h>

#include "../common/simple_fixture.hpp"

constexpr auto kLogLevel = spdlog::level::warn;

auto main(int argc, char* argv[]) -> int {
  spdlog::set_level(kLogLevel);
  spdlog::set_pattern("[%l] %v");

  // Suppress Bazel test sharding warnings
  setenv("TEST_SHARD_INDEX", "0", 0);
  setenv("TEST_TOTAL_SHARDS", "1", 0);
  setenv("TEST_SHARD_STATUS_FILE", "", 0);

  return Catch::Session().run(argc, argv);
}

namespace slangd::semantic {

using slangd::test::SimpleTestFixture;

TEST_CASE(
    "ShouldIndexForDocumentSymbols filters genvar correctly",
    "[symbol_utils]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module test_module;
      logic signal;
      generate
        for (genvar i = 0; i < 4; i++) begin : gen_block
          logic loop_signal;
        end
      endgenerate
    endmodule
  )";

  auto index = fixture.CompileSource(code);

  // The most important test: genvar filtering
  // This is critical for VSCode compatibility and document symbol quality
  auto symbols = index->GetDocumentSymbols("file:///test.sv");

  std::function<bool(
      const std::vector<lsp::DocumentSymbol>&, const std::string&)>
      contains_symbol_named = [&](const auto& syms, const auto& name) -> bool {
    for (const auto& sym : syms) {
      if (sym.name == name) {
        return true;
      }
      if (sym.children.has_value()) {
        if (contains_symbol_named(*sym.children, name)) {
          return true;
        }
      }
    }
    return false;
  };

  // Genvar 'i' should be filtered out of document symbols
  REQUIRE(!contains_symbol_named(symbols, "i"));

  // But meaningful symbols should still be there
  REQUIRE(contains_symbol_named(symbols, "test_module"));
  REQUIRE(contains_symbol_named(symbols, "signal"));
  REQUIRE(contains_symbol_named(symbols, "gen_block"));
}

TEST_CASE("ConvertToLspKind handles complex type aliases", "[symbol_utils]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module test_module;
      typedef enum logic [1:0] {
        IDLE, ACTIVE, DONE
      } state_t;

      typedef struct {
        logic [7:0] data;
        logic valid;
      } packet_t;
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  auto symbols = index->GetDocumentSymbols("file:///test.sv");

  // Find enum and struct typedefs and verify correct LSP kinds
  std::function<void(const std::vector<lsp::DocumentSymbol>&)> check_symbols;
  check_symbols = [&](const auto& syms) {
    for (const auto& sym : syms) {
      if (sym.name == "state_t") {
        REQUIRE(sym.kind == lsp::SymbolKind::kEnum);
      }
      if (sym.name == "packet_t") {
        REQUIRE(sym.kind == lsp::SymbolKind::kStruct);
      }
      if (sym.children.has_value()) {
        check_symbols(*sym.children);
      }
    }
  };

  check_symbols(symbols);
}

TEST_CASE(
    "ComputeLspRange handles symbols without locations", "[symbol_utils]") {
  auto source_manager = std::make_shared<slang::SourceManager>();

  // Create a mock symbol without location for edge case testing
  slang::Bag options;
  auto compilation = std::make_unique<slang::ast::Compilation>(options);

  const auto& root = compilation->getRoot();

  // Test that ComputeLspRange doesn't crash with symbols without location
  auto range = ComputeLspRange(root, *source_manager);

  // Should return zero range for symbols without location
  REQUIRE(range.start.line == 0);
  REQUIRE(range.start.character == 0);
  REQUIRE(range.end.line == 0);
  REQUIRE(range.end.character == 0);
}

}  // namespace slangd::semantic

#include <cstdlib>
#include <string>

#include <catch2/catch_all.hpp>
#include <spdlog/spdlog.h>

#include "../common/syntax_fixture.hpp"

constexpr auto kLogLevel = spdlog::level::debug;

auto main(int argc, char* argv[]) -> int {
  spdlog::set_level(kLogLevel);
  spdlog::set_pattern("[%l] %v");

  setenv("TEST_SHARD_INDEX", "0", 0);
  setenv("TEST_TOTAL_SHARDS", "1", 0);
  setenv("TEST_SHARD_STATUS_FILE", "", 0);

  return Catch::Session().run(argc, argv);
}

using Fixture = slangd::test::SyntaxDocumentSymbolFixture;

TEST_CASE("SyntaxDocumentSymbolVisitor module works", "[syntax_symbols]") {
  std::string code = R"(
    module test_module;
    endmodule
  )";

  auto result = Fixture::BuildSymbols(code);
  Fixture::AssertSymbolExists(result, "test_module", lsp::SymbolKind::kModule);
}

TEST_CASE(
    "SyntaxDocumentSymbolVisitor module with variables works",
    "[syntax_symbols]") {
  std::string code = R"(
    module test_module;
      logic signal_a;
      logic [7:0] signal_b;
    endmodule
  )";

  auto result = Fixture::BuildSymbols(code);
  Fixture::AssertSymbolExists(result, "test_module", lsp::SymbolKind::kModule);
  Fixture::AssertSymbolExists(result, "signal_a", lsp::SymbolKind::kVariable);
  Fixture::AssertSymbolExists(result, "signal_b", lsp::SymbolKind::kVariable);
}

TEST_CASE(
    "SyntaxDocumentSymbolVisitor typedef enum works", "[syntax_symbols]") {
  std::string code = R"(
    module test_module;
      typedef enum logic [1:0] {
        IDLE,
        ACTIVE,
        DONE
      } state_t;
    endmodule
  )";

  auto result = Fixture::BuildSymbols(code);
  Fixture::AssertSymbolExists(result, "state_t", lsp::SymbolKind::kClass);
}

TEST_CASE(
    "SyntaxDocumentSymbolVisitor class with fields works", "[syntax_symbols]") {
  std::string code = R"(
    class TestClass;
      logic field_a;
    endclass
  )";

  auto result = Fixture::BuildSymbols(code);
  Fixture::AssertSymbolExists(result, "TestClass", lsp::SymbolKind::kClass);
  Fixture::AssertSymbolExists(result, "field_a", lsp::SymbolKind::kVariable);
}

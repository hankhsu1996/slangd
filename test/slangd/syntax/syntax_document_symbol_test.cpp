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
using lsp::SymbolKind;

TEST_CASE("SyntaxDocumentSymbolVisitor module works", "[syntax_symbols]") {
  std::string code = R"(
    module test_module;
    endmodule
  )";

  auto result = Fixture::BuildSymbols(code);
  Fixture::AssertSymbol(result, {"test_module"}, SymbolKind::kModule);
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
  Fixture::AssertSymbol(result, {"test_module"}, SymbolKind::kModule);
  Fixture::AssertSymbol(
      result, {"test_module", "signal_a"}, SymbolKind::kVariable);
  Fixture::AssertSymbol(
      result, {"test_module", "signal_b"}, SymbolKind::kVariable);
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
  Fixture::AssertSymbol(result, {"test_module"}, SymbolKind::kModule);
  Fixture::AssertSymbol(result, {"test_module", "state_t"}, SymbolKind::kEnum);
}

TEST_CASE(
    "SyntaxDocumentSymbolVisitor class with fields works", "[syntax_symbols]") {
  std::string code = R"(
    class TestClass;
      logic field_a;
    endclass
  )";

  auto result = Fixture::BuildSymbols(code);
  Fixture::AssertSymbol(result, {"TestClass"}, SymbolKind::kClass);
  Fixture::AssertSymbol(
      result, {"TestClass", "field_a"}, SymbolKind::kVariable);
}

TEST_CASE("SyntaxDocumentSymbolVisitor package works", "[syntax_symbols]") {
  std::string code = R"(
    package test_pkg;
      logic pkg_signal;
    endpackage
  )";

  auto result = Fixture::BuildSymbols(code);
  Fixture::AssertSymbol(result, {"test_pkg"}, SymbolKind::kPackage);
  Fixture::AssertSymbol(
      result, {"test_pkg", "pkg_signal"}, SymbolKind::kVariable);
}

TEST_CASE("SyntaxDocumentSymbolVisitor interface works", "[syntax_symbols]") {
  std::string code = R"(
    interface test_if;
      logic if_signal;
    endinterface
  )";

  auto result = Fixture::BuildSymbols(code);
  Fixture::AssertSymbol(result, {"test_if"}, SymbolKind::kInterface);
  Fixture::AssertSymbol(
      result, {"test_if", "if_signal"}, SymbolKind::kVariable);
}

TEST_CASE("SyntaxDocumentSymbolVisitor function works", "[syntax_symbols]") {
  std::string code = R"(
    module test_module;
      function logic test_func();
        return 1'b0;
      endfunction
    endmodule
  )";

  auto result = Fixture::BuildSymbols(code);
  Fixture::AssertSymbol(result, {"test_module"}, SymbolKind::kModule);
  Fixture::AssertSymbol(
      result, {"test_module", "test_func"}, SymbolKind::kFunction);
}

TEST_CASE("SyntaxDocumentSymbolVisitor task works", "[syntax_symbols]") {
  std::string code = R"(
    module test_module;
      task test_task();
        $display("hello");
      endtask
    endmodule
  )";

  auto result = Fixture::BuildSymbols(code);
  Fixture::AssertSymbol(result, {"test_module"}, SymbolKind::kModule);
  Fixture::AssertSymbol(
      result, {"test_module", "test_task"}, SymbolKind::kFunction);
}

TEST_CASE(
    "SyntaxDocumentSymbolVisitor typedef enum with children works",
    "[syntax_symbols]") {
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
  Fixture::AssertSymbol(result, {"test_module"}, SymbolKind::kModule);
  Fixture::AssertSymbol(result, {"test_module", "state_t"}, SymbolKind::kEnum);
  Fixture::AssertSymbol(
      result, {"test_module", "state_t", "IDLE"}, SymbolKind::kEnumMember);
  Fixture::AssertSymbol(
      result, {"test_module", "state_t", "ACTIVE"}, SymbolKind::kEnumMember);
  Fixture::AssertSymbol(
      result, {"test_module", "state_t", "DONE"}, SymbolKind::kEnumMember);
}

TEST_CASE(
    "SyntaxDocumentSymbolVisitor typedef struct with children works",
    "[syntax_symbols]") {
  std::string code = R"(
    package test_pkg;
      typedef struct {
        logic [7:0] data;
        logic valid;
        logic [15:0] address;
      } packet_t;
    endpackage
  )";

  auto result = Fixture::BuildSymbols(code);
  Fixture::AssertSymbol(result, {"test_pkg"}, SymbolKind::kPackage);
  Fixture::AssertSymbol(result, {"test_pkg", "packet_t"}, SymbolKind::kStruct);
  Fixture::AssertSymbol(
      result, {"test_pkg", "packet_t", "data"}, SymbolKind::kField);
  Fixture::AssertSymbol(
      result, {"test_pkg", "packet_t", "valid"}, SymbolKind::kField);
  Fixture::AssertSymbol(
      result, {"test_pkg", "packet_t", "address"}, SymbolKind::kField);
}

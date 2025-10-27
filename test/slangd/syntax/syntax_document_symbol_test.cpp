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
using enum lsp::SymbolKind;

TEST_CASE("SyntaxDocumentSymbolVisitor module works", "[syntax_symbols]") {
  std::string code = R"(
    module top;
    endmodule
  )";

  auto result = Fixture::BuildSymbols(code);
  Fixture::AssertSymbol(result, {"top"}, kModule);
}

TEST_CASE(
    "SyntaxDocumentSymbolVisitor module with variables works",
    "[syntax_symbols]") {
  std::string code = R"(
    module top;
      logic signal_a;
      logic [7:0] signal_b;
    endmodule
  )";

  auto result = Fixture::BuildSymbols(code);
  Fixture::AssertSymbol(result, {"top"}, kModule);
  Fixture::AssertSymbol(result, {"top", "signal_a"}, kVariable);
  Fixture::AssertSymbol(result, {"top", "signal_b"}, kVariable);
}

TEST_CASE(
    "SyntaxDocumentSymbolVisitor typedef enum works", "[syntax_symbols]") {
  std::string code = R"(
    module top;
      typedef enum logic [1:0] {
        IDLE,
        ACTIVE,
        DONE
      } state_t;
    endmodule
  )";

  auto result = Fixture::BuildSymbols(code);
  Fixture::AssertSymbol(result, {"top"}, kModule);
  Fixture::AssertSymbol(result, {"top", "state_t"}, kEnum);
}

TEST_CASE(
    "SyntaxDocumentSymbolVisitor class with fields works", "[syntax_symbols]") {
  std::string code = R"(
    class TestClass;
      logic field_a;
    endclass
  )";

  auto result = Fixture::BuildSymbols(code);
  Fixture::AssertSymbol(result, {"TestClass"}, kClass);
  Fixture::AssertSymbol(result, {"TestClass", "field_a"}, kVariable);
}

TEST_CASE("SyntaxDocumentSymbolVisitor package works", "[syntax_symbols]") {
  std::string code = R"(
    package pkg;
      logic pkg_signal;
    endpackage
  )";

  auto result = Fixture::BuildSymbols(code);
  Fixture::AssertSymbol(result, {"pkg"}, kPackage);
  Fixture::AssertSymbol(result, {"pkg", "pkg_signal"}, kVariable);
}

TEST_CASE("SyntaxDocumentSymbolVisitor interface works", "[syntax_symbols]") {
  std::string code = R"(
    interface bus;
      logic if_signal;
    endinterface
  )";

  auto result = Fixture::BuildSymbols(code);
  Fixture::AssertSymbol(result, {"bus"}, kInterface);
  Fixture::AssertSymbol(result, {"bus", "if_signal"}, kVariable);
}

TEST_CASE("SyntaxDocumentSymbolVisitor function works", "[syntax_symbols]") {
  std::string code = R"(
    module top;
      function logic my_func();
        return 1'b0;
      endfunction
    endmodule
  )";

  auto result = Fixture::BuildSymbols(code);
  Fixture::AssertSymbol(result, {"top"}, kModule);
  Fixture::AssertSymbol(result, {"top", "my_func"}, kFunction);
}

TEST_CASE("SyntaxDocumentSymbolVisitor task works", "[syntax_symbols]") {
  std::string code = R"(
    module top;
      task my_task();
        $display("hello");
      endtask
    endmodule
  )";

  auto result = Fixture::BuildSymbols(code);
  Fixture::AssertSymbol(result, {"top"}, kModule);
  Fixture::AssertSymbol(result, {"top", "my_task"}, kFunction);
}

TEST_CASE(
    "SyntaxDocumentSymbolVisitor typedef enum with children works",
    "[syntax_symbols]") {
  std::string code = R"(
    module top;
      typedef enum logic [1:0] {
        IDLE,
        ACTIVE,
        DONE
      } state_t;
    endmodule
  )";

  auto result = Fixture::BuildSymbols(code);
  Fixture::AssertSymbol(result, {"top"}, kModule);
  Fixture::AssertSymbol(result, {"top", "state_t"}, kEnum);
  Fixture::AssertSymbol(result, {"top", "state_t", "IDLE"}, kEnumMember);
  Fixture::AssertSymbol(result, {"top", "state_t", "ACTIVE"}, kEnumMember);
  Fixture::AssertSymbol(result, {"top", "state_t", "DONE"}, kEnumMember);
}

TEST_CASE(
    "SyntaxDocumentSymbolVisitor typedef struct with children works",
    "[syntax_symbols]") {
  std::string code = R"(
    package pkg;
      typedef struct {
        logic [7:0] data;
        logic valid;
        logic [15:0] address;
      } packet_t;
    endpackage
  )";

  auto result = Fixture::BuildSymbols(code);
  Fixture::AssertSymbol(result, {"pkg"}, kPackage);
  Fixture::AssertSymbol(result, {"pkg", "packet_t"}, kStruct);
  Fixture::AssertSymbol(result, {"pkg", "packet_t", "data"}, kField);
  Fixture::AssertSymbol(result, {"pkg", "packet_t", "valid"}, kField);
  Fixture::AssertSymbol(result, {"pkg", "packet_t", "address"}, kField);
}

TEST_CASE("SyntaxDocumentSymbolVisitor ports work", "[syntax_symbols]") {
  std::string code = R"(
    module top(
      input logic clk,
      input logic [7:0] data_in,
      output logic [7:0] data_out
    );
    endmodule
  )";

  auto result = Fixture::BuildSymbols(code);
  Fixture::AssertSymbol(result, {"top"}, kModule);
  Fixture::AssertSymbol(result, {"top", "clk"}, kVariable);
  Fixture::AssertSymbol(result, {"top", "data_in"}, kVariable);
  Fixture::AssertSymbol(result, {"top", "data_out"}, kVariable);
}

TEST_CASE("SyntaxDocumentSymbolVisitor parameters work", "[syntax_symbols]") {
  std::string code = R"(
    module top #(
      parameter WIDTH = 8,
      parameter DEPTH = 16
    )();
    endmodule
  )";

  auto result = Fixture::BuildSymbols(code);
  Fixture::AssertSymbol(result, {"top"}, kModule);
  Fixture::AssertSymbol(result, {"top", "WIDTH"}, kConstant);
  Fixture::AssertSymbol(result, {"top", "DEPTH"}, kConstant);
}

TEST_CASE("SyntaxDocumentSymbolVisitor nets work", "[syntax_symbols]") {
  std::string code = R"(
    module top;
      wire clk_wire;
      reg [7:0] data_reg;
    endmodule
  )";

  auto result = Fixture::BuildSymbols(code);
  Fixture::AssertSymbol(result, {"top"}, kModule);
  Fixture::AssertSymbol(result, {"top", "clk_wire"}, kVariable);
  Fixture::AssertSymbol(result, {"top", "data_reg"}, kVariable);
}

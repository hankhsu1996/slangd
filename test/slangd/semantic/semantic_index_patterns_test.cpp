#include <algorithm>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_all.hpp>
#include <spdlog/spdlog.h>

#include "../common/simple_fixture.hpp"
#include "slangd/semantic/semantic_index.hpp"

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

// Helper function to extract symbol names from index
auto GetSymbolNames(const SemanticIndex& index) -> std::vector<std::string> {
  std::vector<std::string> names;
  for (const auto& [loc, info] : index.GetAllSymbols()) {
    names.emplace_back(info.symbol->name);
  }
  return names;
}

// Helper function to check if symbols exist
auto HasSymbols(
    const SemanticIndex& index, const std::vector<std::string>& expected)
    -> bool {
  auto names = GetSymbolNames(index);
  return std::ranges::all_of(expected, [&names](const std::string& symbol) {
    return std::ranges::find(names, symbol) != names.end();
  });
}

TEST_CASE(
    "SemanticIndex handles interface ports without crash", "[semantic_index]") {
  SimpleTestFixture fixture;

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

    auto index = fixture.CompileSource(source);
    REQUIRE(index != nullptr);
    REQUIRE(index->GetSymbolCount() > 0);
    REQUIRE(HasSymbols(*index, {"internal_var"}));
  }

  SECTION("undefined interface - single file resilience") {
    const std::string source = R"(
      module processor(undefined_if bus);
        assign bus.signal = 1'b1;
        assign bus.data = 32'hDEAD;
        logic internal_state;
        logic [7:0] counter;
      endmodule
    )";

    auto index = fixture.CompileSource(source);
    REQUIRE(index != nullptr);
    REQUIRE(index->GetSymbolCount() > 0);
    REQUIRE(HasSymbols(*index, {"internal_state", "counter"}));
  }

  SECTION("interface in always_comb conditions and RHS") {
    const std::string source = R"(
      module generic_module(generic_if iface);
        logic state;
        logic [7:0] counter;
        logic enable;

        always_comb begin
          if (enable & ~iface.ready) begin
            state = 1'b0;
          end else if (enable & iface.ready) begin
            if (iface.mode == 1'b1) begin
              state = 1'b1;
            end else begin
              counter = iface.data;
            end
          end
        end
      endmodule
    )";

    auto index = fixture.CompileSource(source);
    REQUIRE(index != nullptr);
    REQUIRE(index->GetSymbolCount() > 0);
    REQUIRE(HasSymbols(*index, {"state", "counter", "enable"}));
  }
}

TEST_CASE(
    "SemanticIndex handles complex SystemVerilog patterns",
    "[semantic_index]") {
  SimpleTestFixture fixture;

  SECTION("nested scope definitions") {
    const std::string source = R"(
      module m;
        if (1) begin : named_block
          logic nested_signal;
          always_ff @(posedge clk) begin
            logic deeply_nested;
          end
        end
      endmodule
    )";

    auto index = fixture.CompileSource(source);
    REQUIRE(index != nullptr);
    REQUIRE(
        HasSymbols(*index, {"nested_signal", "deeply_nested", "named_block"}));
  }

  SECTION("multiple declarations on single line") {
    const std::string source = R"(
      module m;
        logic sig1, sig2, sig3;
        logic [7:0] byte1, byte2, byte3;
        wire w1, w2, w3;
      endmodule
    )";

    auto index = fixture.CompileSource(source);

    const std::vector<std::string> expected = {
        "sig1", "sig2", "sig3", "byte1", "byte2", "byte3", "w1", "w2", "w3"};
    REQUIRE(HasSymbols(*index, expected));
  }

  SECTION("reference tracking in expressions") {
    const std::string source = R"(
      module m;
        logic a, b, c;
        logic [7:0] result;

        always_comb begin
          result = a ? b : c;
          if (a && b) begin
            result = 8'hFF;
          end
        end
      endmodule
    )";

    auto index = fixture.CompileSource(source);
    const auto& refs = index->GetReferences();

    REQUIRE(!refs.empty());
    REQUIRE(HasSymbols(*index, {"a", "b", "c", "result"}));
  }

  SECTION("typedef and enum definitions") {
    const std::string source = R"(
      module m;
        typedef logic [31:0] word_t;
        typedef enum logic [1:0] {
          IDLE = 2'b00,
          ACTIVE = 2'b01,
          DONE = 2'b10
        } state_t;

        word_t data;
        state_t current_state;
      endmodule
    )";

    auto index = fixture.CompileSource(source);

    // Should find most symbols (enum values may have different indexing
    // behavior)
    REQUIRE(HasSymbols(*index, {"word_t", "state_t", "data", "current_state"}));
  }

  SECTION("package definitions") {
    const std::string source = R"(
      package test_pkg;
        parameter WIDTH = 32;
        typedef logic [WIDTH-1:0] data_t;
      endpackage
    )";

    auto index = fixture.CompileSource(source);
    REQUIRE(index != nullptr);
    REQUIRE(index->GetSymbolCount() > 0);
    REQUIRE(HasSymbols(*index, {"test_pkg", "WIDTH", "data_t"}));
  }

  SECTION("struct and union types") {
    const std::string source = R"(
      module m;
        typedef struct packed {
          logic [7:0] header;
          logic [23:0] payload;
        } packet_t;

        typedef union packed {
          logic [31:0] word;
          logic [7:0][3:0] bytes;
        } data_t;

        packet_t pkt;
        data_t data;
      endmodule
    )";

    auto index = fixture.CompileSource(source);
    REQUIRE(index != nullptr);
    REQUIRE(index->GetSymbolCount() > 0);
    REQUIRE(HasSymbols(*index, {"packet_t", "data_t", "pkt"}));
  }

  SECTION("module with package imports") {
    const std::string source = R"(
      package test_pkg;
        parameter WIDTH = 32;
        typedef logic [WIDTH-1:0] data_t;
      endpackage

      module test_module;
        import test_pkg::*;
        data_t test_signal;
      endmodule
    )";

    auto index = fixture.CompileSource(source);

    const std::vector<std::string> expected = {
        "test_pkg", "test_module", "test_signal", "WIDTH", "data_t"};
    REQUIRE(HasSymbols(*index, expected));
  }
}

}  // namespace slangd::semantic

#include <cstdlib>
#include <memory>
#include <string>

#include <catch2/catch_all.hpp>
#include <slang/ast/Symbol.h>
#include <slang/util/Enum.h>
#include <spdlog/spdlog.h>

#include "../../common/semantic_fixture.hpp"

constexpr auto kLogLevel = spdlog::level::debug;

auto main(int argc, char* argv[]) -> int {
  spdlog::set_level(kLogLevel);
  spdlog::set_pattern("[%l] %v");

  // Suppress Bazel test sharding warnings
  setenv("TEST_SHARD_INDEX", "0", 0);
  setenv("TEST_TOTAL_SHARDS", "1", 0);
  setenv("TEST_SHARD_STATUS_FILE", "", 0);

  return Catch::Session().run(argc, argv);
}

using Fixture = slangd::test::SemanticTestFixture;

TEST_CASE("SemanticIndex net self-definition lookup works", "[definition]") {
  std::string code = R"(
    module net_test;
      wire [31:0] bus_data;
      tri [15:0] tri_signal;
      supply0 gnd;
      supply1 vdd;
      uwire logic reset_n;
    endmodule
  )";

  auto result = Fixture::BuildIndex(code);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "bus_data", 0, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "tri_signal", 0, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "gnd", 0, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "vdd", 0, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "reset_n", 0, 0);
}

TEST_CASE(
    "SemanticIndex net reference go-to-definition works", "[definition]") {
  std::string code = R"(
    module net_reference_test;
      wire [31:0] bus_data;
      tri [15:0] tri_signal;
      supply0 gnd;
      supply1 vdd;
      wire result;

      // Net usage in assign statements
      assign bus_data = 32'h1234;
      assign tri_signal = bus_data[15:0];
      assign result = gnd || vdd;
    endmodule
  )";

  auto result = Fixture::BuildIndex(code);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "bus_data", 1, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "bus_data", 2, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "tri_signal", 1, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "gnd", 1, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "vdd", 1, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "result", 1, 0);
}

TEST_CASE("SemanticIndex complex net expressions work", "[definition]") {
  std::string code = R"(
    module complex_net_test;
      wire [31:0] data_in;
      wire [31:0] data_out;
      wire [7:0] addr;
      tri enable;
      supply0 gnd;

      // Complex expressions with multiple net references
      assign data_out = enable ? data_in : 32'h0;
      assign addr = data_in[7:0] & 8'hFF;

      // Nested expressions
      wire intermediate;
      assign intermediate = (data_in != 32'h0) && enable;
      assign data_out = intermediate ? (data_in + 1) : gnd;
    endmodule
  )";

  auto result = Fixture::BuildIndex(code);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "data_in", 1, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "data_out", 1, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "enable", 1, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "data_in", 2, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "addr", 1, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "data_in", 3, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "enable", 2, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "intermediate", 1, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "data_in", 4, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "gnd", 1, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "data_out", 2, 0);
}

TEST_CASE("SemanticIndex multiple net declarations work", "[definition]") {
  std::string code = R"(
    module multi_net_test;
      // Multiple nets in one declaration
      wire a, b, c;
      tri [7:0] x, y, z;
      supply0 gnd0, gnd1;

      // References to each net
      assign a = 1'b1;
      assign b = a;
      assign c = b;

      assign x = 8'h01;
      assign y = x + 1;
      assign z = y & 8'hF0;
    endmodule
  )";

  auto result = Fixture::BuildIndex(code);

  // Test self-definitions for multiple nets in one declaration
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "a", 0, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "b", 0, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "c", 0, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "x", 0, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "y", 0, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "z", 0, 0);

  // Test references to these nets
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "a", 1, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "b", 1, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "x", 1, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "y", 1, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "z", 1, 0);
}

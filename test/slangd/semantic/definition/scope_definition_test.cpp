#include <cstdlib>
#include <memory>
#include <string>

#include <catch2/catch_all.hpp>
#include <slang/ast/Symbol.h>
#include <slang/util/Enum.h>
#include <spdlog/spdlog.h>

#include "../../common/simple_fixture.hpp"

constexpr auto kLogLevel = spdlog::level::debug;  // Always debug for tests

auto main(int argc, char* argv[]) -> int {
  spdlog::set_level(kLogLevel);
  spdlog::set_pattern("[%l] %v");

  // Suppress Bazel test sharding warnings
  setenv("TEST_SHARD_INDEX", "0", 0);
  setenv("TEST_TOTAL_SHARDS", "1", 0);
  setenv("TEST_SHARD_STATUS_FILE", "", 0);

  return Catch::Session().run(argc, argv);
}

using slangd::test::SimpleTestFixture;

TEST_CASE("SemanticIndex module end label reference works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module Test;
    endmodule : Test
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "Test", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "Test", 1, 0);
}

TEST_CASE("SemanticIndex package end label reference works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    package TestPkg;
    endpackage : TestPkg
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "TestPkg", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "TestPkg", 1, 0);
}

TEST_CASE("SemanticIndex wildcard import reference works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    package test_pkg;
      parameter int IMPORTED_PARAM = 16;
    endpackage

    module wildcard_import_test;
      import test_pkg::*;
      logic [IMPORTED_PARAM-1:0] data;
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "IMPORTED_PARAM", 1, 0);
}

TEST_CASE("SemanticIndex explicit import reference works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    package explicit_pkg;
      parameter int SPECIFIC_PARAM = 8;
    endpackage

    module explicit_import_test;
      import explicit_pkg::SPECIFIC_PARAM;
      parameter int WIDTH = SPECIFIC_PARAM;
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "SPECIFIC_PARAM", 1, 0);
}

TEST_CASE(
    "SemanticIndex module header import reference works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    package header_pkg;
      typedef logic [7:0] byte_t;
    endpackage

    module header_import_test
      import header_pkg::*;
      ();
      byte_t data_byte;
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "byte_t", 1, 0);
}

TEST_CASE("SemanticIndex local scope import reference works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    package local_pkg;
      parameter int LOCAL_WIDTH = 12;
    endpackage

    module local_import_test;
      initial begin
        import local_pkg::*;
        logic [LOCAL_WIDTH-1:0] local_data;
      end
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "LOCAL_WIDTH", 1, 0);
}

TEST_CASE(
    "SemanticIndex generate block self-definition works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module gen_block_test;
      generate
        if (1) begin : named_gen
          logic signal;
        end
      endgenerate
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "named_gen", 0, 0);
}

TEST_CASE(
    "SemanticIndex generate block array self-definition works",
    "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module gen_array_test;
      genvar i;
      generate
        for (i = 0; i < 4; i = i + 1) begin : gen_loop
          logic [i:0] bus;
        end
      endgenerate
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "gen_loop", 0, 0);
}

TEST_CASE(
    "SemanticIndex genvar self-definition outside generate works",
    "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module genvar_outside_test;
      genvar i;
      generate
        for (i = 0; i < 4; i = i + 1) begin : gen_loop
          logic data;
        end
      endgenerate
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "i", 0, 0);
}

TEST_CASE(
    "SemanticIndex genvar self-definition inside generate works",
    "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module genvar_inside_test;
      generate
        for (genvar j = 0; j < 2; j = j + 1) begin : inline_gen
          logic data;
        end
      endgenerate
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "j", 0, 0);
}

TEST_CASE(
    "SemanticIndex for-loop generate parameter references in loop expressions",
    "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module loop_param_refs;
      parameter int START = 0;
      parameter int END = 4;
      for (genvar i = START; i < END; i++) begin : gen_loop
        logic data;
      end
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "START", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "END", 1, 0);
  // Note: genvar references in loop expressions resolve to temporary loop
  // variable, not the genvar declaration. This is a Slang limitation.
}

TEST_CASE(
    "SemanticIndex external genvar loop references work", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module external_genvar_test;
      parameter int COUNT = 2;
      genvar idx;
      generate
        for (idx = 0; idx < COUNT; idx = idx + 1) begin : array_gen
          logic data;
        end
      endgenerate
    endmodule
  )";

  auto index = fixture.CompileSource(code);

  // Test genvar self-definition
  fixture.AssertGoToDefinition(*index, code, "idx", 0, 0);

  // Test parameter reference in loop control
  fixture.AssertGoToDefinition(*index, code, "COUNT", 1, 0);

  // Test genvar references in loop control expressions
  // Note: idx = 0 is NOT indexed (it's syntax-only, not a bound expression)
  // Occurrences: [0] genvar idx, [1] idx = 0 (skip), [2] idx < COUNT, [3]
  // idx++, [4] idx + 1
  fixture.AssertGoToDefinition(*index, code, "idx", 2, 0);  // idx < COUNT
  fixture.AssertGoToDefinition(*index, code, "idx", 3, 0);  // idx++ (first idx)
  fixture.AssertGoToDefinition(*index, code, "idx", 4, 0);  // idx + 1
}

TEST_CASE("SemanticIndex inline genvar loop references work", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module inline_genvar_test;
      parameter int SIZE = 3;
      generate
        for (genvar entry = 0; entry < SIZE; entry = entry + 1) begin : inline_array_gen
          logic value;
        end
      endgenerate
    endmodule
  )";

  auto index = fixture.CompileSource(code);

  // Test genvar self-definition
  fixture.AssertGoToDefinition(*index, code, "entry", 0, 0);

  // Test parameter reference in loop control
  fixture.AssertGoToDefinition(*index, code, "SIZE", 1, 0);

  // Test genvar references in loop control expressions
  // Occurrences: [0] genvar entry, [1] entry < SIZE, [2] entry++, [3] entry + 1
  fixture.AssertGoToDefinition(*index, code, "entry", 1, 0);  // entry < SIZE
  fixture.AssertGoToDefinition(
      *index, code, "entry", 2, 0);  // entry++ (first entry)
  fixture.AssertGoToDefinition(*index, code, "entry", 3, 0);  // entry + 1
}

TEST_CASE("SemanticIndex nested generate blocks work", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module nested_gen_test;
      genvar i, j;
      generate
        for (i = 0; i < 2; i = i + 1) begin : outer_gen
          for (j = 0; j < 3; j = j + 1) begin : inner_gen
            logic [i+j:0] combined_bus;
          end
        end
      endgenerate
    endmodule
  )";

  auto index = fixture.CompileSource(code);

  // Test generate block self-definitions (these work)
  fixture.AssertGoToDefinition(*index, code, "outer_gen", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "inner_gen", 0, 0);

  // Test genvar self-definitions (these work)
  fixture.AssertGoToDefinition(*index, code, "i", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "j", 0, 0);

  // NOTE: Genvar reference tests not included
  // See docs/SEMANTIC_INDEXING.md "Known Limitations"
}

TEST_CASE(
    "SemanticIndex generate if conditional parameter references",
    "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module gen_if_param_test;
      parameter int THRESHOLD = 2;
      genvar i;
      for (i = 0; i < 4; i++) begin : gen_loop
        if (i >= THRESHOLD) begin
          logic active;
        end
      end
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  // Test parameter reference in generate if condition
  fixture.AssertGoToDefinition(*index, code, "THRESHOLD", 1, 0);
}

TEST_CASE(
    "SemanticIndex generate if else condition expressions indexed",
    "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module gen_if_else_test;
      parameter bit ENABLE_A = 1;
      parameter bit ENABLE_B = 0;

      if (ENABLE_A) begin : mode_a
        logic signal_a;
      end
      else if (ENABLE_B) begin : mode_b
        logic signal_b;
      end
      else begin : mode_default
        logic signal_default;
      end
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  // Test parameters in if/else conditions are indexed
  fixture.AssertGoToDefinition(*index, code, "ENABLE_A", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "ENABLE_B", 1, 0);

  // Test that symbols in all branches are indexed (covered by visitDefault)
  fixture.AssertGoToDefinition(*index, code, "signal_a", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "signal_b", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "signal_default", 0, 0);
}

TEST_CASE(
    "SemanticIndex generate case condition and item expressions indexed",
    "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module gen_case_test;
      parameter int MODE_A = 0;
      parameter int MODE_B = 1;
      parameter int MODE_C = 2;
      parameter int SELECTOR = 1;

      case (SELECTOR)
        MODE_A: begin : case_a
          logic signal_a;
        end
        MODE_B: begin : case_b
          logic signal_b;
        end
        MODE_C: begin : case_c
          logic signal_c;
        end
        default: begin : case_default
          logic signal_default;
        end
      endcase
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  // Test case condition parameter is indexed
  fixture.AssertGoToDefinition(*index, code, "SELECTOR", 1, 0);

  // Test case item parameters are indexed (go-to-definition from case items)
  fixture.AssertGoToDefinition(*index, code, "MODE_A", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "MODE_B", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "MODE_C", 1, 0);

  // Test that symbols inside case branches are also indexed
  fixture.AssertGoToDefinition(*index, code, "signal_a", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "signal_b", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "signal_c", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "signal_default", 0, 0);
}

TEST_CASE("SemanticIndex package name in function call works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    package util_pkg;
      function automatic bit is_valid(int val);
        return val > 0;
      endfunction
    endpackage

    module test;
      parameter int SIZE = 10;
      if (util_pkg::is_valid(SIZE)) begin
        logic data;
      end
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  // Test clicking on package name in function call
  fixture.AssertGoToDefinition(*index, code, "util_pkg", 1, 0);
}

TEST_CASE(
    "SemanticIndex package name in parameter reference works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    package cfg_pkg;
      parameter int BUS_WIDTH = 32;
    endpackage

    module test;
      logic [cfg_pkg::BUS_WIDTH-1:0] bus;
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  // Test clicking on package name when accessing parameter
  fixture.AssertGoToDefinition(*index, code, "cfg_pkg", 1, 0);
}

TEST_CASE(
    "SemanticIndex package name in typedef reference works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    package types_pkg;
      typedef logic [7:0] byte_t;
    endpackage

    module test;
      types_pkg::byte_t data;
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  // Test clicking on package name when using typedef
  fixture.AssertGoToDefinition(*index, code, "types_pkg", 1, 0);
}

TEST_CASE(
    "SemanticIndex package name in class reference works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    package class_pkg;
      class Transaction;
        int id;
      endclass
    endpackage

    module test;
      class_pkg::Transaction txn;
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  // Test clicking on package name when using class type
  fixture.AssertGoToDefinition(*index, code, "class_pkg", 1, 0);
}

#include <cstdlib>
#include <memory>
#include <string>

#include <catch2/catch_all.hpp>
#include <slang/ast/Symbol.h>
#include <slang/util/Enum.h>
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

TEST_CASE("SemanticIndex module self-definition lookup works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module empty_module;
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "empty_module", 0, 0);
}

TEST_CASE(
    "SemanticIndex parameter self-definition lookup works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module param_test;
      parameter int WIDTH = 8;
      parameter logic ENABLE = 1'b1;
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "WIDTH", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "ENABLE", 0, 0);
}

TEST_CASE(
    "SemanticIndex typedef self-definition lookup works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module typedef_test;
      typedef logic [7:0] byte_t;
      typedef logic [15:0] word_t;
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "byte_t", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "word_t", 0, 0);
}

TEST_CASE("SemanticIndex type cast reference lookup works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module typecast_test;
      typedef logic [7:0] unique_cast_type;
      logic [7:0] result;

      always_comb begin
        result = unique_cast_type'(8'h42);
      end
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "unique_cast_type", 1, 0);
}

TEST_CASE(
    "SemanticIndex variable declaration parameter reference works",
    "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module param_ref_test;
      localparam int BUS_WIDTH = 8;
      logic [BUS_WIDTH-1:0] data_bus;
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "BUS_WIDTH", 1, 0);
}

TEST_CASE(
    "SemanticIndex unpacked variable dimension parameter reference works",
    "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module unpacked_test;
      localparam int ARRAY_SIZE = 16;
      logic data_array[ARRAY_SIZE-1:0];
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "ARRAY_SIZE", 1, 0);
}

TEST_CASE(
    "SemanticIndex bit select dimension parameter reference works",
    "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module bit_select_test;
      localparam int INDEX_WIDTH = 4;
      logic bit_array[INDEX_WIDTH];
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "INDEX_WIDTH", 1, 0);
}

TEST_CASE(
    "SemanticIndex ascending range dimension parameter reference works",
    "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module ascending_test;
      localparam int WIDTH = 8;
      logic [0:WIDTH-1] ascending_bus;
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "WIDTH", 1, 0);
}

TEST_CASE(
    "SemanticIndex queue dimension parameter reference works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module queue_test;
      localparam int MAX_QUEUE_SIZE = 16;
      int bounded_queue[$:MAX_QUEUE_SIZE];
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "MAX_QUEUE_SIZE", 1, 0);
}

TEST_CASE(
    "SemanticIndex typedef packed dimensions comprehensive test",
    "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module typedef_packed_comprehensive;
      localparam int WIDTH1 = 8;
      localparam int WIDTH2 = 4;

      // Simple range in packed typedef
      typedef logic [WIDTH1-1:0] simple_packed_t;

      // Ascending range in packed typedef
      typedef logic [0:WIDTH2-1] ascending_packed_t;
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  // Test both width parameters are found in their respective typedef usages
  fixture.AssertGoToDefinition(*index, code, "WIDTH1", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "WIDTH2", 1, 0);
}

TEST_CASE(
    "SemanticIndex typedef unpacked dimensions comprehensive test",
    "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module typedef_unpacked_comprehensive;
      localparam int ARRAY_SIZE = 16;
      localparam int DEPTH = 32;

      // Range select in unpacked typedef
      typedef logic unpacked_range_t[ARRAY_SIZE-1:0];

      // Bit select in unpacked typedef
      typedef int unpacked_bit_t[DEPTH];
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "ARRAY_SIZE", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "DEPTH", 1, 0);
}

TEST_CASE(
    "SemanticIndex variable declaration comprehensive dimension test",
    "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module var_decl_comprehensive;
      localparam int PACKED_W = 8;
      localparam int UNPACKED_W = 16;
      localparam int QUEUE_MAX = 32;

      // Packed dimensions on variable
      logic [PACKED_W-1:0] packed_var;

      // Unpacked dimensions on variable
      logic unpacked_var[UNPACKED_W-1:0];

      // Queue dimension on variable
      int queue_var[$:QUEUE_MAX];
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "PACKED_W", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "UNPACKED_W", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "QUEUE_MAX", 1, 0);
}

TEST_CASE(
    "SemanticIndex multi-dimensional parameter references work",
    "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module multi_dim_test;
      localparam int DIM1 = 4;
      localparam int DIM2 = 8;

      // Multi-dimensional array with parameters
      logic multi_array[DIM1][DIM2-1:0];
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "DIM1", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "DIM2", 1, 0);
}

TEST_CASE(
    "SemanticIndex packed typedef parameter reference works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module test_packed;
      localparam int PACKED_WIDTH = 8;
      typedef logic [PACKED_WIDTH-1:0] packed_bus_t;
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "PACKED_WIDTH", 1, 0);
}

TEST_CASE(
    "SemanticIndex unpacked typedef parameter go-to-definition",
    "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module test_unpacked_dims;
      localparam int ARRAY_SIZE = 16;
      typedef logic unpacked_array_t[ARRAY_SIZE-1:0];
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "ARRAY_SIZE", 1, 0);
}

TEST_CASE("Parameter self-definition lookup works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module test;
      parameter int WIDTH = 8;
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "WIDTH", 0, 0);
}

TEST_CASE(
    "SemanticIndex reference tracking basic functionality", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module test_module;
      logic signal;
      typedef logic [7:0] byte_t;

      initial begin
        signal = 1'b0;  // Reference to signal
      end
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertReferenceExists(*index, code, "signal", 1);
  fixture.AssertGoToDefinition(*index, code, "signal", 1, 0);
}

TEST_CASE("Module self-definition lookup works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module test_module;
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "test_module", 0, 0);
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
  fixture.AssertGoToDefinition(*index, code, "SPECIFIC_PARAM", 2, 0);
}

TEST_CASE(
    "SemanticIndex module header import reference works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    package header_pkg;
      typedef logic [7:0] byte_t;
    endpackage

    module header_import_test import header_pkg::*;
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

TEST_CASE("SemanticIndex task go-to-definition works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module task_test;
      task my_task(input int a, output int b);
        b = a + 1;
      endtask

      initial begin
        int result;
        my_task(5, result);
      end
    endmodule
  )";

  auto index = fixture.CompileSource(code);

  // Test self-definition (clicking on task declaration)
  fixture.AssertGoToDefinition(*index, code, "my_task", 0, 0);

  // Test call reference (clicking on task call)
  fixture.AssertGoToDefinition(*index, code, "my_task", 1, 0);
}

TEST_CASE("SemanticIndex function go-to-definition works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module function_test;
      function int my_function(input int x);
        return x * 2;
      endfunction

      initial begin
        $display("Result: %d", my_function(5));
      end
    endmodule
  )";

  auto index = fixture.CompileSource(code);

  // Test self-definition (clicking on function declaration)
  fixture.AssertGoToDefinition(*index, code, "my_function", 0, 0);

  // Test call reference (clicking on function call)
  fixture.AssertGoToDefinition(*index, code, "my_function", 1, 0);
}

TEST_CASE("SemanticIndex function argument reference works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module function_arg_test;
      function int my_function(input int x, input int y);
        return x + y;
      endfunction
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "x", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "y", 1, 0);
}

TEST_CASE("SemanticIndex task argument reference works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module task_arg_test;
      task my_task(input int a, output int b, inout int c);
        b = a + c;
      endtask
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "a", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "b", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "c", 1, 0);
}

TEST_CASE(
    "SemanticIndex function return type reference works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module return_type_test;
      typedef logic [7:0] byte_t;
      
      function byte_t get_byte(input int index);
        return byte_t'(index);
      endfunction
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "byte_t", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "byte_t", 2, 0);
}

TEST_CASE(
    "SemanticIndex function outer scope reference works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module outer_scope_test;
      localparam int CONSTANT = 42;
      logic [7:0] shared_var;
      
      function int get_constant();
        return CONSTANT + shared_var;
      endfunction
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "CONSTANT", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "shared_var", 1, 0);
}

TEST_CASE(
    "SemanticIndex function implicit return variable works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module implicit_return_test;
      function int my_func(input int x);
        my_func = x * 2;  // Function name as implicit return variable
      endfunction
      
      initial begin
        $display("Result: %d", my_func(5));
      end
    endmodule
  )";

  auto index = fixture.CompileSource(code);

  // Test function definition (first occurrence)
  fixture.AssertGoToDefinition(*index, code, "my_func", 0, 0);

  // Test implicit return variable usage (second occurrence)
  fixture.AssertGoToDefinition(*index, code, "my_func", 1, 0);

  // Test function call (third occurrence)
  fixture.AssertGoToDefinition(*index, code, "my_func", 2, 0);
}

TEST_CASE(
    "SemanticIndex package function explicit import works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    package math_pkg;
      function int add_one(input int value);
        return value + 1;
      endfunction
      
      task increment_task(inout int value);
        value = value + 1;
      endtask
    endpackage

    module package_import_test;
      import math_pkg::add_one;
      import math_pkg::increment_task;
      
      initial begin
        int result = add_one(5);
        increment_task(result);
      end
    endmodule
  )";

  auto index = fixture.CompileSource(code);

  // Test function definition in package
  fixture.AssertGoToDefinition(*index, code, "add_one", 0, 0);

  // Test task definition in package
  fixture.AssertGoToDefinition(*index, code, "increment_task", 0, 0);

  // Test explicit import references
  fixture.AssertGoToDefinition(*index, code, "add_one", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "increment_task", 1, 0);

  // Test function/task calls
  fixture.AssertGoToDefinition(*index, code, "add_one", 2, 0);
  fixture.AssertGoToDefinition(*index, code, "increment_task", 2, 0);
}

// ===== ENUM SUPPORT TEST CASES =====

TEST_CASE("SemanticIndex enum value self-definition works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    typedef enum logic [1:0] {
      STATE_IDLE,
      STATE_BUSY,
      STATE_DONE
    } state_t;
  )";

  auto index = fixture.CompileSource(code);

  // Test enum value self-definitions
  fixture.AssertGoToDefinition(*index, code, "STATE_IDLE", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "STATE_BUSY", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "STATE_DONE", 0, 0);
}

TEST_CASE("SemanticIndex enum value reference works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    typedef enum logic [1:0] {
      STATE_IDLE,
      STATE_BUSY,
      STATE_DONE
    } state_t;

    module enum_test;
      state_t current = STATE_IDLE;
      initial begin
        current = STATE_BUSY;
        if (current == STATE_DONE) begin
          $display("Done");
        end
      end
    endmodule
  )";

  auto index = fixture.CompileSource(code);

  // Test enum value references go to their definitions
  fixture.AssertGoToDefinition(*index, code, "STATE_IDLE", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "STATE_BUSY", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "STATE_DONE", 1, 0);
}

TEST_CASE("SemanticIndex package enum explicit import works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    package enum_pkg;
      typedef enum {
        PKG_STATE_A,
        PKG_STATE_B,
        PKG_STATE_C
      } pkg_state_t;
    endpackage

    module package_enum_test;
      import enum_pkg::PKG_STATE_A;
      import enum_pkg::PKG_STATE_B;
      import enum_pkg::pkg_state_t;
      
      initial begin
        pkg_state_t state = PKG_STATE_A;
        state = PKG_STATE_B;
      end
    endmodule
  )";

  auto index = fixture.CompileSource(code);

  // Test enum value definitions in package
  fixture.AssertGoToDefinition(*index, code, "PKG_STATE_A", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "PKG_STATE_B", 0, 0);

  // Test explicit import references
  fixture.AssertGoToDefinition(*index, code, "PKG_STATE_A", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "PKG_STATE_B", 1, 0);

  // Test enum value usage
  fixture.AssertGoToDefinition(*index, code, "PKG_STATE_A", 2, 0);
  fixture.AssertGoToDefinition(*index, code, "PKG_STATE_B", 2, 0);
}

TEST_CASE("SemanticIndex package enum wildcard import works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    package wild_enum_pkg;
      typedef enum {
        WILD_A,
        WILD_B,
        WILD_C
      } wild_enum_t;
    endpackage

    module wildcard_enum_test;
      import wild_enum_pkg::*;
      
      initial begin
        wild_enum_t state = WILD_A;
        state = WILD_B;
        if (state != WILD_C) begin
          $display("Not WILD_C");
        end
      end
    endmodule
  )";

  auto index = fixture.CompileSource(code);

  // Test enum value definitions in package
  fixture.AssertGoToDefinition(*index, code, "WILD_A", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "WILD_B", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "WILD_C", 0, 0);

  // Test enum value usage through wildcard import
  fixture.AssertGoToDefinition(*index, code, "WILD_A", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "WILD_B", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "WILD_C", 1, 0);
}

TEST_CASE("SemanticIndex anonymous enum works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module anon_enum_test;
      enum {
        ANON_FIRST,
        ANON_SECOND,
        ANON_THIRD
      } anon_state;
      
      initial begin
        anon_state = ANON_FIRST;
        anon_state = ANON_SECOND;
      end
    endmodule
  )";

  auto index = fixture.CompileSource(code);

  // Test anonymous enum value definitions and references
  fixture.AssertGoToDefinition(*index, code, "ANON_FIRST", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "ANON_SECOND", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "ANON_FIRST", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "ANON_SECOND", 1, 0);
}

// ===== STRUCT/UNION SUPPORT TEST CASES =====

TEST_CASE("SemanticIndex struct field member access works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    typedef struct {
      logic [31:0] data;
      logic        valid;
      logic [7:0]  id;
    } packet_t;

    module struct_test;
      packet_t pkt;
      
      initial begin
        pkt.data = 32'h1234;
        pkt.valid = 1'b1;
        pkt.id = 8'hAB;
        
        if (pkt.valid && pkt.data != 0) begin
          $display("ID: %h", pkt.id);
        end
      end
    endmodule
  )";

  auto index = fixture.CompileSource(code);

  // Test struct field member access references
  fixture.AssertGoToDefinition(*index, code, "data", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "valid", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "id", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "valid", 2, 0);
  fixture.AssertGoToDefinition(*index, code, "data", 2, 0);
  fixture.AssertGoToDefinition(*index, code, "id", 2, 0);
}

TEST_CASE("SemanticIndex nested struct member access works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    typedef struct {
      logic [31:0] data;
      logic        valid;
    } header_t;

    typedef struct {
      header_t header;
      logic [7:0] payload[0:15];
    } frame_t;

    module nested_struct_test;
      frame_t frame;
      
      initial begin
        frame.header.data = 32'hABCD;
        frame.header.valid = 1'b1;
        frame.payload[0] = 8'h01;
      end
    endmodule
  )";

  auto index = fixture.CompileSource(code);

  // Test nested struct member access
  fixture.AssertGoToDefinition(*index, code, "header", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "data", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "header", 2, 0);
  fixture.AssertGoToDefinition(*index, code, "valid", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "payload", 1, 0);
}

TEST_CASE("SemanticIndex union member access works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    typedef union {
      logic [31:0] word;
      logic [7:0]  bytes[4];
      struct {
        logic [15:0] low;
        logic [15:0] high;
      } halves;
    } word_union_t;

    module union_test;
      word_union_t wu;
      
      initial begin
        wu.word = 32'h12345678;
        wu.bytes[0] = 8'hAB;
        wu.halves.low = 16'hCDEF;
        wu.halves.high = 16'h9876;
      end
    endmodule
  )";

  auto index = fixture.CompileSource(code);

  // Test union member access
  fixture.AssertGoToDefinition(*index, code, "word", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "bytes", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "halves", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "low", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "halves", 2, 0);
  fixture.AssertGoToDefinition(*index, code, "high", 1, 0);
}

TEST_CASE(
    "SemanticIndex package struct explicit import works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    package struct_pkg;
      typedef struct {
        logic clk;
        logic reset;
        logic [7:0] data;
      } control_t;
    endpackage

    module package_struct_test;
      import struct_pkg::control_t;
      
      control_t ctrl;
      
      initial begin
        ctrl.clk = 1'b0;
        ctrl.reset = 1'b1;
        ctrl.data = 8'h00;
      end
    endmodule
  )";

  auto index = fixture.CompileSource(code);

  // Test struct field member access from package
  fixture.AssertGoToDefinition(*index, code, "clk", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "reset", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "data", 1, 0);
}

TEST_CASE("SemanticIndex direct struct declaration works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module direct_struct_test;
      struct {
        int x;
        int y;
      } point;
      
      initial begin
        point.x = 10;
        point.y = 20;
      end
    endmodule
  )";

  auto index = fixture.CompileSource(code);

  // Test direct struct field access (not typedef)
  fixture.AssertGoToDefinition(*index, code, "x", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "y", 1, 0);
}

TEST_CASE("SemanticIndex net self-definition lookup works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module net_test;
      wire [31:0] bus_data;
      tri [15:0] tri_signal;
      supply0 gnd;
      supply1 vdd;
      uwire logic reset_n;
    endmodule
  )";

  auto index = fixture.CompileSource(code);

  // Test net self-definitions
  fixture.AssertGoToDefinition(*index, code, "bus_data", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "tri_signal", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "gnd", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "vdd", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "reset_n", 0, 0);
}

TEST_CASE(
    "SemanticIndex net reference go-to-definition works", "[definition]") {
  SimpleTestFixture fixture;
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

  auto index = fixture.CompileSource(code);

  // Test net references go to their definitions
  fixture.AssertGoToDefinition(*index, code, "bus_data", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "bus_data", 2, 0);
  fixture.AssertGoToDefinition(*index, code, "tri_signal", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "gnd", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "vdd", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "result", 1, 0);
}

TEST_CASE("SemanticIndex complex net expressions work", "[definition]") {
  SimpleTestFixture fixture;
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

  auto index = fixture.CompileSource(code);

  // Test complex expressions with net references
  fixture.AssertGoToDefinition(*index, code, "enable", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "data_in", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "data_out", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "data_in", 2, 0);
  fixture.AssertGoToDefinition(*index, code, "addr", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "data_in", 3, 0);
  fixture.AssertGoToDefinition(*index, code, "enable", 2, 0);
  fixture.AssertGoToDefinition(*index, code, "intermediate", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "intermediate", 2, 0);
  fixture.AssertGoToDefinition(*index, code, "data_in", 4, 0);
  fixture.AssertGoToDefinition(*index, code, "gnd", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "data_out", 2, 0);
}

TEST_CASE("SemanticIndex multiple net declarations work", "[definition]") {
  SimpleTestFixture fixture;
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
      assign y = x;
      assign z = y;
    endmodule
  )";

  auto index = fixture.CompileSource(code);

  // Test multiple net declarations
  fixture.AssertGoToDefinition(*index, code, "a", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "b", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "c", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "x", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "y", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "z", 0, 0);

  // Test references to each net
  fixture.AssertGoToDefinition(*index, code, "a", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "a", 2, 0);
  fixture.AssertGoToDefinition(*index, code, "b", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "b", 2, 0);
  fixture.AssertGoToDefinition(*index, code, "c", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "x", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "x", 2, 0);
  fixture.AssertGoToDefinition(*index, code, "y", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "y", 2, 0);
  fixture.AssertGoToDefinition(*index, code, "z", 1, 0);
}

TEST_CASE("SemanticIndex port self-definition lookup works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module port_test(
      input  logic clk,
      output logic valid,
      input  logic [31:0] data
    );
      always_ff @(posedge clk) begin
        valid <= (data > 0) ? 1'b1 : 1'b0;
      end
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "clk", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "valid", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "data", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "clk", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "valid", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "data", 1, 0);
}

}  // namespace slangd::semantic

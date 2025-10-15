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

TEST_CASE(
    "SemanticIndex typedef self-definition lookup works", "[definition]") {
  std::string code = R"(
    module typedef_test;
      typedef logic [7:0] byte_t;
      typedef logic [15:0] word_t;
    endmodule
  )";

  auto result = Fixture::BuildIndex(code);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "byte_t", 0, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "word_t", 0, 0);
}

TEST_CASE("SemanticIndex type cast reference lookup works", "[definition]") {
  std::string code = R"(
    module typecast_test;
      typedef logic [7:0] unique_cast_type;
      logic [7:0] result;

      always_comb begin
        result = unique_cast_type'(8'h42);
      end
    endmodule
  )";

  auto result = Fixture::BuildIndex(code);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "unique_cast_type", 1, 0);
}

TEST_CASE(
    "SemanticIndex complex typedef cast should compile correctly",
    "[definition]") {
  std::string code = R"(
    typedef struct packed { logic [7:0] x, y; } complex_t;

    module complex_test;
      complex_t result;

      always_comb begin
        result = complex_t'(16'h1234);
      end
    endmodule
  )";

  auto result = Fixture::BuildIndex(code);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "complex_t", 1, 0);
}

TEST_CASE(
    "SemanticIndex constant size cast reference lookup works", "[definition]") {
  std::string code = R"(
    module sizecast_test;
      parameter WIDTH = 8;
      logic [7:0] result;

      always_comb begin
        result = WIDTH'(4);
      end
    endmodule
  )";

  auto result = Fixture::BuildIndex(code);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "WIDTH", 1, 0);
}

TEST_CASE(
    "SemanticIndex variable size cast reference lookup works", "[definition]") {
  std::string code = R"(
    module sizecast_variable_test;
      parameter WIDTH = 8;
      parameter SIZE = 4;
      logic [7:0] result;
      logic [3:0] input_val;

      always_comb begin
        result = WIDTH'(input_val);
        result = SIZE'(result - WIDTH'(1));
      end
    endmodule
  )";

  auto result = Fixture::BuildIndex(code);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "WIDTH", 1, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "SIZE", 1, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "WIDTH", 2, 0);
}

TEST_CASE(
    "SemanticIndex parameter type in module port list works", "[definition]") {
  std::string code = R"(
    typedef enum logic [1:0] {
      ALU_KIND,
      FPU_KIND,
      LSU_KIND
    } t_unit_kind;

    module test_unit
    #(
      parameter t_unit_kind UNIT_TYPE = ALU_KIND
    )
    ();
    endmodule
  )";

  auto result = Fixture::BuildIndex(code);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "t_unit_kind", 1, 0);
}

TEST_CASE(
    "SemanticIndex parameter type inside module body works", "[definition]") {
  std::string code = R"(
    typedef struct {
      logic [7:0] data;
      logic valid;
    } t_bus_data;

    module test_module();
      parameter t_bus_data DEFAULT_DATA = '{data: 8'h00, valid: 1'b0};
    endmodule
  )";

  auto result = Fixture::BuildIndex(code);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "t_bus_data", 1, 0);
}

TEST_CASE(
    "SemanticIndex localparam type definition reference works",
    "[definition]") {
  std::string code = R"(
    typedef union packed {
      logic [15:0] word;
      logic [15:0] bytes;
    } t_data_union;

    module data_processor();
      localparam t_data_union INIT_DATA = 16'hFFFF;
    endmodule
  )";

  auto result = Fixture::BuildIndex(code);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "t_data_union", 1, 0);
}

TEST_CASE("SemanticIndex package type in parameter works", "[definition]") {
  std::string code = R"(
    package config_pkg;
      typedef enum logic [1:0] {
        MODE_NORMAL,
        MODE_TEST,
        MODE_DEBUG
      } t_mode;
    endpackage

    module processor
      import config_pkg::*;
    #(
      parameter config_pkg::t_mode OPERATING_MODE = MODE_NORMAL
    )
    ();
    endmodule
  )";

  auto result = Fixture::BuildIndex(code);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "t_mode", 1, 0);
}

TEST_CASE("SemanticIndex enum type in parameter works", "[definition]") {
  std::string code = R"(
    typedef enum logic [2:0] {
      STATE_IDLE   = 3'b001,
      STATE_ACTIVE = 3'b010,
      STATE_DONE   = 3'b100
    } t_state;

    module fsm_controller
    #(
      parameter t_state RESET_STATE = STATE_IDLE
    )
    ();
    endmodule
  )";

  auto result = Fixture::BuildIndex(code);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "t_state", 1, 0);
}

TEST_CASE(
    "SemanticIndex mixed parameter types comprehensive test", "[definition]") {
  std::string code = R"(
    typedef logic [7:0] byte_t;
    typedef enum { RED, GREEN, BLUE } color_t;
    typedef struct { int x, y; } point_t;

    module comprehensive_test
    #(
      parameter byte_t WIDTH = 8'hFF,
      parameter color_t DEFAULT_COLOR = RED,
      parameter point_t ORIGIN = '{x: 0, y: 0}
    )
    ();
      localparam byte_t INTERNAL_WIDTH = WIDTH;
      localparam color_t INTERNAL_COLOR = DEFAULT_COLOR;
      localparam point_t INTERNAL_POINT = ORIGIN;
    endmodule
  )";

  auto result = Fixture::BuildIndex(code);

  // Test all parameter type references in port list
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "byte_t", 1, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "color_t", 1, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "point_t", 1, 0);

  // Test all parameter type references in body
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "byte_t", 2, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "color_t", 2, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "point_t", 2, 0);
}

TEST_CASE("SemanticIndex enum value self-definition works", "[definition]") {
  std::string code = R"(
    typedef enum logic [1:0] {
      STATE_IDLE,
      STATE_BUSY,
      STATE_DONE
    } state_t;
  )";

  auto result = Fixture::BuildIndex(code);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "STATE_IDLE", 0, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "STATE_BUSY", 0, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "STATE_DONE", 0, 0);
}

TEST_CASE("SemanticIndex enum value reference works", "[definition]") {
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

  auto result = Fixture::BuildIndex(code);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "STATE_IDLE", 1, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "STATE_BUSY", 1, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "STATE_DONE", 1, 0);
}

TEST_CASE("SemanticIndex anonymous enum works", "[definition]") {
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

  auto result = Fixture::BuildIndex(code);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "ANON_FIRST", 0, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "ANON_SECOND", 0, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "ANON_FIRST", 1, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "ANON_SECOND", 1, 0);
}

TEST_CASE("SemanticIndex struct field member access works", "[definition]") {
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

  auto result = Fixture::BuildIndex(code);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "data", 1, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "valid", 1, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "id", 1, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "valid", 2, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "data", 2, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "id", 2, 0);
}

TEST_CASE("SemanticIndex nested struct member access works", "[definition]") {
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

  auto result = Fixture::BuildIndex(code);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "header", 1, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "data", 1, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "header", 2, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "valid", 1, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "payload", 1, 0);
}

TEST_CASE("SemanticIndex union member access works", "[definition]") {
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

  auto result = Fixture::BuildIndex(code);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "word", 1, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "bytes", 1, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "halves", 1, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "low", 1, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "halves", 2, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "high", 1, 0);
}

TEST_CASE("SemanticIndex direct struct declaration works", "[definition]") {
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

  auto result = Fixture::BuildIndex(code);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "x", 1, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "y", 1, 0);
}

TEST_CASE(
    "SemanticIndex typedef used in both declaration and cast expression",
    "[definition]") {
  std::string code = R"(
    module test;
      typedef logic [7:0] counter_t;

      // Variable declarations using the typedef
      counter_t count_a;
      counter_t count_b, count_c;

      // Assignment with type cast using same typedef
      initial begin
        count_a = counter_t'(5);
        count_b = counter_t'(10);
      end
    endmodule
  )";

  auto result = Fixture::BuildIndex(code);

  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "counter_t", 1, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "counter_t", 2, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "counter_t", 3, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "counter_t", 4, 0);
}

TEST_CASE(
    "SemanticIndex system function type argument reference works",
    "[definition]") {
  std::string code = R"(
    module system_func_type_test;
      typedef logic [7:0] byte_t;
      typedef struct packed { logic [3:0] addr; } packet_t;

      localparam int BYTE_WIDTH = $bits(byte_t);
      localparam int PACKET_WIDTH = $bits(packet_t);

      logic [$bits(byte_t)-1:0] data;
    endmodule
  )";

  auto result = Fixture::BuildIndex(code);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "byte_t", 1, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "packet_t", 1, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "byte_t", 2, 0);
}

TEST_CASE(
    "SemanticIndex assignment pattern field references work", "[definition]") {
  std::string code = R"(
    typedef enum logic [1:0] {
      MODE_A,
      MODE_B,
      MODE_C
    } mode_type;

    typedef struct {
      mode_type mode;
      logic [4:0] index;
    } config_t;

    module assignment_pattern_test;
      parameter config_t DEFAULT_CONFIG = '{mode: MODE_A, index: 5'b0};
      parameter config_t ALTERNATE_CONFIG = '{mode: MODE_B, index: 5'b1};
    endmodule
  )";

  auto result = Fixture::BuildIndex(code);

  // Test field name references in assignment patterns
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "mode", 1, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "index", 1, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "mode", 2, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "index", 2, 0);

  // Test enum value references in assignment patterns
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "MODE_A", 1, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "MODE_B", 1, 0);
}

TEST_CASE(
    "SemanticIndex typed assignment pattern type reference works",
    "[definition]") {
  std::string code = R"(
    typedef struct {
      logic [3:0] field_a;
      logic [4:0] field_b;
    } config_t;

    module typed_assignment_pattern_test;
      config_t cfg;

      initial begin
        cfg = config_t'{field_a: 4'h5, field_b: 5'h0A};
      end
    endmodule
  )";

  auto result = Fixture::BuildIndex(code);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "config_t", 1, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "field_a", 1, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "field_b", 1, 0);
}

TEST_CASE("SemanticIndex enum base type reference works", "[definition]") {
  std::string code = R"(
    typedef logic [19:0] base_type_t;

    typedef enum base_type_t {
      VALUE_A = 20'b0000,
      VALUE_B = 20'b0001,
      VALUE_C = 20'b0010
    } enum_name_t;

    module enum_base_test;
      enum_name_t signal;
    endmodule
  )";

  auto result = Fixture::BuildIndex(code);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "base_type_t", 1, 0);
}

TEST_CASE(
    "SemanticIndex function call in size cast type expression works",
    "[definition]") {
  std::string code = R"(
    module func_in_cast_test;
      parameter SIZE = 8;

      function automatic int calc_width(int size);
        return size - 1;
      endfunction

      logic [15:0] input_val;
      logic [15:0] result;

      always_comb begin
        result = (calc_width(SIZE))'(input_val + SIZE);
      end
    endmodule
  )";

  auto result = Fixture::BuildIndex(code);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "calc_width", 1, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "SIZE", 1, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "SIZE", 2, 0);
}

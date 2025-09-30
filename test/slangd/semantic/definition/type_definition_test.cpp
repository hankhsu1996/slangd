#include <cstdlib>
#include <memory>
#include <string>

#include <catch2/catch_all.hpp>
#include <slang/ast/Symbol.h>
#include <slang/util/Enum.h>
#include <spdlog/spdlog.h>

#include "../../common/simple_fixture.hpp"

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

using slangd::test::SimpleTestFixture;

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
    "SemanticIndex complex typedef cast should compile correctly",
    "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    typedef struct packed { logic [7:0] x, y; } complex_t;
    
    module complex_test;
      complex_t result;
      
      always_comb begin
        result = complex_t'(16'h1234);
      end
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "complex_t", 1, 0);
}

TEST_CASE(
    "SemanticIndex parameter type in module port list works", "[definition]") {
  SimpleTestFixture fixture;
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

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "t_unit_kind", 1, 0);
}

TEST_CASE(
    "SemanticIndex parameter type inside module body works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    typedef struct {
      logic [7:0] data;
      logic valid;
    } t_bus_data;

    module test_module();
      parameter t_bus_data DEFAULT_DATA = '{data: 8'h00, valid: 1'b0};
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "t_bus_data", 1, 0);
}

TEST_CASE(
    "SemanticIndex localparam type definition reference works",
    "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    typedef union packed {
      logic [15:0] word;
      logic [7:0]  bytes [2];
    } t_data_union;

    module data_processor();
      localparam t_data_union INIT_DATA = 16'hFFFF;
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "t_data_union", 1, 0);
}

TEST_CASE("SemanticIndex package type in parameter works", "[definition]") {
  SimpleTestFixture fixture;
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

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "t_mode", 1, 0);
}

TEST_CASE("SemanticIndex enum type in parameter works", "[definition]") {
  SimpleTestFixture fixture;
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

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "t_state", 1, 0);
}

TEST_CASE(
    "SemanticIndex mixed parameter types comprehensive test", "[definition]") {
  SimpleTestFixture fixture;
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

  auto index = fixture.CompileSource(code);

  // Test all parameter type references in port list
  fixture.AssertGoToDefinition(*index, code, "byte_t", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "color_t", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "point_t", 1, 0);

  // Test all parameter type references in body
  fixture.AssertGoToDefinition(*index, code, "byte_t", 2, 0);
  fixture.AssertGoToDefinition(*index, code, "color_t", 2, 0);
  fixture.AssertGoToDefinition(*index, code, "point_t", 2, 0);
}

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
  fixture.AssertGoToDefinition(*index, code, "STATE_IDLE", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "STATE_BUSY", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "STATE_DONE", 1, 0);
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
  fixture.AssertGoToDefinition(*index, code, "ANON_FIRST", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "ANON_SECOND", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "ANON_FIRST", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "ANON_SECOND", 1, 0);
}

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
  fixture.AssertGoToDefinition(*index, code, "word", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "bytes", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "halves", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "low", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "halves", 2, 0);
  fixture.AssertGoToDefinition(*index, code, "high", 1, 0);
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
  fixture.AssertGoToDefinition(*index, code, "x", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "y", 1, 0);
}

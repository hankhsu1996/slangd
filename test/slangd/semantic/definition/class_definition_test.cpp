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

TEST_CASE("SemanticIndex class self-definition works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    class Counter;
    endclass
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "Counter", 0, 0);
}

TEST_CASE("SemanticIndex class reference in variable works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    class Packet;
    endclass

    module test;
      Packet pkt;
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "Packet", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "Packet", 1, 0);
}

TEST_CASE(
    "SemanticIndex parameterized class self-definition works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    class Buffer #(parameter int SIZE = 8);
    endclass
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "Buffer", 0, 0);
}

TEST_CASE("SemanticIndex virtual class self-definition works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    virtual class BaseClass;
    endclass
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "BaseClass", 0, 0);
}

TEST_CASE(
    "SemanticIndex class property self-definition works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    class Test;
      int data;
    endclass
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "data", 0, 0);
}

TEST_CASE(
    "SemanticIndex class property reference in method works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    class Counter;
      int value;
      function void increment();
        value = value + 1;
      endfunction
    endclass
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "value", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "value", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "value", 2, 0);
}

TEST_CASE("SemanticIndex class parameter reference works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    class Buffer #(parameter int SIZE = 8);
      int data[SIZE];
    endclass

    module test;
      Buffer b;
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "SIZE", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "SIZE", 1, 0);
}

TEST_CASE("SemanticIndex multiple class properties work", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    class Packet;
      int header;
      int payload;
      function void init();
        header = 0;
        payload = 0;
      endfunction
    endclass
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "header", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "header", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "payload", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "payload", 1, 0);
}

TEST_CASE(
    "SemanticIndex class specialization name reference works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    package pkg;
      class Counter #(parameter int MAX_VAL = 100);
        static function int saturate_add(int a);
          return (a > MAX_VAL) ? MAX_VAL : a;
        endfunction
      endclass
    endpackage

    module test;
      int x = pkg::Counter#(.MAX_VAL(50))::saturate_add(75);
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "Counter", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "Counter", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "saturate_add", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "saturate_add", 1, 0);
}

TEST_CASE(
    "SemanticIndex class specialization parameter name reference works",
    "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    package pkg;
      class Counter #(parameter int MAX_VAL = 100);
        static function int saturate_add(int a);
          return (a > MAX_VAL) ? MAX_VAL : a;
        endfunction
      endclass
    endpackage

    module test;
      int x = pkg::Counter#(.MAX_VAL(50))::saturate_add(75);
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "MAX_VAL", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "MAX_VAL", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "MAX_VAL", 2, 0);
  fixture.AssertGoToDefinition(*index, code, "MAX_VAL", 3, 0);
}

TEST_CASE(
    "SemanticIndex class specialization same parameters cached",
    "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    package pkg;
      class Config #(parameter int WIDTH = 16);
        static function int get_width();
          return WIDTH;
        endfunction
      endclass
    endpackage

    module test;
      int x = pkg::Config#(.WIDTH(32))::get_width();
      int y = pkg::Config#(.WIDTH(32))::get_width();
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "WIDTH", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "WIDTH", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "WIDTH", 2, 0);
  fixture.AssertGoToDefinition(*index, code, "WIDTH", 3, 0);
}

TEST_CASE(
    "SemanticIndex class specialization different parameters", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    package pkg;
      class Config #(parameter int WIDTH = 16);
        static function int get_width();
          return WIDTH;
        endfunction
      endclass
    endpackage

    module test;
      int x = pkg::Config#(.WIDTH(32))::get_width();
      int y = pkg::Config#(.WIDTH(64))::get_width();
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "WIDTH", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "WIDTH", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "WIDTH", 2, 0);
  fixture.AssertGoToDefinition(*index, code, "WIDTH", 3, 0);
}

TEST_CASE(
    "SemanticIndex class parameter without instantiation", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    class Buffer #(parameter int SIZE = 8);
      int data[SIZE];
    endclass
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "SIZE", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "SIZE", 1, 0);
}

TEST_CASE("SemanticIndex class instance member access works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    class Packet;
      int data;
    endclass

    module test;
      Packet pkt = new;
      initial pkt.data = 5;
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "data", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "data", 1, 0);
}

TEST_CASE("SemanticIndex class member access via this works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    class Counter;
      int value;
      function void set(int v);
        this.value = v;
      endfunction
    endclass
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "value", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "value", 1, 0);
}

TEST_CASE(
    "SemanticIndex class constructor argument navigation works",
    "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    class Buffer;
      function new(int size);
      endfunction
    endclass

    module test;
      int sz = 16;
      Buffer b = new(sz);
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "sz", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "sz", 1, 0);
}

TEST_CASE(
    "SemanticIndex multiple class instances member access works",
    "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    class Point;
      int x;
      int y;
    endclass

    module test;
      Point p1 = new;
      Point p2 = new;
      initial begin
        p1.x = 10;
        p2.y = 20;
      end
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "x", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "x", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "y", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "y", 1, 0);
}

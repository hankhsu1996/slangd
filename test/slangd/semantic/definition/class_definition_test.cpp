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

TEST_CASE("SemanticIndex class self-definition works", "[definition]") {
  std::string code = R"(
    class Counter;
    endclass : Counter
  )";

  auto result = Fixture::BuildIndex(code);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "Counter", 0, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "Counter", 1, 0);
}

TEST_CASE("SemanticIndex class reference in variable works", "[definition]") {
  std::string code = R"(
    class Packet;
    endclass

    module test;
      Packet pkt;
    endmodule
  )";

  auto result = Fixture::BuildIndex(code);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "Packet", 0, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "Packet", 1, 0);
}

TEST_CASE(
    "SemanticIndex parameterized class self-definition works", "[definition]") {
  std::string code = R"(
    class Buffer #(parameter int SIZE = 8);
    endclass
  )";

  auto result = Fixture::BuildIndex(code);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "Buffer", 0, 0);
}

TEST_CASE("SemanticIndex virtual class self-definition works", "[definition]") {
  std::string code = R"(
    virtual class BaseClass;
    endclass
  )";

  auto result = Fixture::BuildIndex(code);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "BaseClass", 0, 0);
}

TEST_CASE(
    "SemanticIndex class property self-definition works", "[definition]") {
  std::string code = R"(
    class Test;
      int data;
    endclass
  )";

  auto result = Fixture::BuildIndex(code);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "data", 0, 0);
}

TEST_CASE(
    "SemanticIndex class property reference in method works", "[definition]") {
  std::string code = R"(
    class Counter;
      int value;
      function void increment();
        value = value + 1;
      endfunction
    endclass
  )";

  auto result = Fixture::BuildIndex(code);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "value", 0, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "value", 1, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "value", 2, 0);
}

TEST_CASE("SemanticIndex class parameter reference works", "[definition]") {
  std::string code = R"(
    class Buffer #(parameter int SIZE = 8);
      int data[SIZE];
    endclass

    module test;
      Buffer b;
    endmodule
  )";

  auto result = Fixture::BuildIndex(code);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "SIZE", 0, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "SIZE", 1, 0);
}

TEST_CASE("SemanticIndex multiple class properties work", "[definition]") {
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

  auto result = Fixture::BuildIndex(code);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "header", 0, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "header", 1, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "payload", 0, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "payload", 1, 0);
}

TEST_CASE(
    "SemanticIndex class specialization name reference works", "[definition]") {
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

  auto result = Fixture::BuildIndex(code);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "Counter", 0, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "Counter", 1, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "saturate_add", 0, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "saturate_add", 1, 0);
}

TEST_CASE(
    "SemanticIndex class specialization parameter name reference works",
    "[definition]") {
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

  auto result = Fixture::BuildIndex(code);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "MAX_VAL", 0, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "MAX_VAL", 1, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "MAX_VAL", 2, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "MAX_VAL", 3, 0);
}

TEST_CASE(
    "SemanticIndex class specialization same parameters cached",
    "[definition]") {
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

  auto result = Fixture::BuildIndex(code);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "WIDTH", 0, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "WIDTH", 1, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "WIDTH", 2, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "WIDTH", 3, 0);
}

TEST_CASE(
    "SemanticIndex class specialization different parameters", "[definition]") {
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

  auto result = Fixture::BuildIndex(code);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "WIDTH", 0, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "WIDTH", 1, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "WIDTH", 2, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "WIDTH", 3, 0);
}

TEST_CASE(
    "SemanticIndex class parameter without instantiation", "[definition]") {
  std::string code = R"(
    class Buffer #(parameter int SIZE = 8);
      int data[SIZE];
    endclass
  )";

  auto result = Fixture::BuildIndex(code);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "SIZE", 0, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "SIZE", 1, 0);
}

TEST_CASE("SemanticIndex class instance member access works", "[definition]") {
  std::string code = R"(
    class Packet;
      int data;
    endclass

    module test;
      Packet pkt = new;
      initial pkt.data = 5;
    endmodule
  )";

  auto result = Fixture::BuildIndex(code);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "data", 0, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "data", 1, 0);
}

TEST_CASE("SemanticIndex class member access via this works", "[definition]") {
  std::string code = R"(
    class Counter;
      int value;
      function void set(int v);
        this.value = v;
      endfunction
    endclass
  )";

  auto result = Fixture::BuildIndex(code);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "value", 0, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "value", 1, 0);
}

TEST_CASE(
    "SemanticIndex class constructor argument navigation works",
    "[definition]") {
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

  auto result = Fixture::BuildIndex(code);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "sz", 0, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "sz", 1, 0);
}

TEST_CASE(
    "SemanticIndex multiple class instances member access works",
    "[definition]") {
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

  auto result = Fixture::BuildIndex(code);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "x", 0, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "x", 1, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "y", 0, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "y", 1, 0);
}

TEST_CASE(
    "SemanticIndex class extends clause navigation works", "[definition]") {
  std::string code = R"(
    class Base;
    endclass

    class Derived extends Base;
    endclass
  )";

  auto result = Fixture::BuildIndex(code);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "Base", 0, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "Base", 1, 0);
}

TEST_CASE(
    "SemanticIndex parameterized class extends clause works", "[definition]") {
  std::string code = R"(
    class Base #(parameter int WIDTH = 8);
    endclass

    class Derived extends Base;
    endclass
  )";

  auto result = Fixture::BuildIndex(code);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "Base", 0, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "Base", 1, 0);
}

TEST_CASE("SemanticIndex class extends with members works", "[definition]") {
  std::string code = R"(
    class Base;
      int base_value;
    endclass

    class Derived extends Base;
      int derived_value;
    endclass
  )";

  auto result = Fixture::BuildIndex(code);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "Base", 0, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "Base", 1, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "base_value", 0, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "derived_value", 0, 0);
}

TEST_CASE(
    "SemanticIndex parameterized class with extends works", "[definition]") {
  std::string code = R"(
    class Base;
    endclass

    class Derived #(parameter int SIZE = 10) extends Base;
    endclass
  )";

  auto result = Fixture::BuildIndex(code);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "Base", 0, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "Base", 1, 0);
}

TEST_CASE(
    "SemanticIndex class specialization with symbol parameter works",
    "[definition]") {
  std::string code = R"(
    package pkg;
      class Config #(parameter int WIDTH = 16);
        static function int get_width();
          return WIDTH;
        endfunction
      endclass
    endpackage

    module test;
      parameter int BUS_WIDTH = 32;
      int x = pkg::Config#(.WIDTH(BUS_WIDTH))::get_width();
    endmodule
  )";

  auto result = Fixture::BuildIndex(code);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "BUS_WIDTH", 0, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "BUS_WIDTH", 1, 0);
}

TEST_CASE(
    "SemanticIndex pure virtual function navigation works", "[definition]") {
  std::string code = R"(
    typedef int reg_t;
    typedef int value_t;

    virtual class BaseHandler;
      pure virtual function void set_value(reg_t addr, value_t data);
      pure virtual function value_t get_value(reg_t addr);
    endclass
  )";

  auto result = Fixture::BuildIndex(code);

  // Function names
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "set_value", 0, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "get_value", 0, 0);

  // Return types
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "value_t", 0, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "value_t", 1, 0);
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "value_t", 2, 0);

  // Argument types
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "reg_t", 0, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "reg_t", 1, 0);
  Fixture::AssertGoToDefinition(*result.index, result.uri, code, "reg_t", 2, 0);

  // Argument variables (each function has its own parameters)
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "addr", 0, 0);  // set_value addr
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "addr", 1, 1);  // get_value addr
  Fixture::AssertGoToDefinition(
      *result.index, result.uri, code, "data", 0, 0);  // set_value data
}

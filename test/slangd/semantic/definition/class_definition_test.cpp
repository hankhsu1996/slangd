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
  // Click on "value" in declaration
  fixture.AssertGoToDefinition(*index, code, "value", 0, 0);
  // Click on first "value" in method (left side of assignment)
  fixture.AssertGoToDefinition(*index, code, "value", 1, 0);
  // Click on second "value" in method (right side of addition)
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
  fixture.AssertGoToDefinition(*index, code, "SIZE", 0, 0);  // Self-definition
  fixture.AssertGoToDefinition(
      *index, code, "SIZE", 1, 0);  // Reference in array
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
  // Test header property
  fixture.AssertGoToDefinition(*index, code, "header", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "header", 1, 0);
  // Test payload property
  fixture.AssertGoToDefinition(*index, code, "payload", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "payload", 1, 0);
}

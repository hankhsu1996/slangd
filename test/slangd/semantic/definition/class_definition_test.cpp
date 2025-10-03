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

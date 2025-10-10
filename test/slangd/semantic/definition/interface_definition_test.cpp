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

TEST_CASE("SemanticIndex interface end label reference works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    interface TestIf;
    endinterface : TestIf
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "TestIf", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "TestIf", 1, 0);
}

TEST_CASE(
    "SemanticIndex interface modport self-definition works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    interface I2C;
      logic sda, scl;

      modport master (
        output sda, scl
      );

      modport slave (
        input sda, scl
      );
    endinterface

    module TestModule;
      I2C i2c_inst();
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "master", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "slave", 0, 0);
}

TEST_CASE(
    "SemanticIndex interface signal self-definition works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    interface MemBus;
      logic [31:0] addr, data;
      logic valid, ready;

      modport cpu (
        output addr, data, valid,
        input ready
      );
    endinterface

    module TestModule;
      MemBus mem_inst();
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "addr", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "data", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "valid", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "ready", 0, 0);
}

TEST_CASE(
    "SemanticIndex interface port in module declaration works",
    "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    interface MemBus;
      logic [31:0] addr, data;
      modport cpu (output addr, data);
    endinterface

    module CPU(
      MemBus.cpu mem_if
    );
      assign mem_if.addr = 32'h1000;
    endmodule
  )";

  auto index = fixture.CompileSource(code);

  fixture.AssertGoToDefinition(*index, code, "MemBus", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "mem_if", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "MemBus", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "cpu", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "addr", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "data", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "cpu", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "addr", 1, 0);
}

TEST_CASE(
    "SemanticIndex interface parameter in packed dimension works",
    "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    interface test_if #(
      parameter int WIDTH = 8
    ) ();
      logic [WIDTH-1:0] data_signal;
    endinterface
  )";

  auto index = fixture.CompileSource(code);

  fixture.AssertGoToDefinition(*index, code, "WIDTH", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "WIDTH", 1, 0);
}

TEST_CASE("SemanticIndex interface member access works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    interface bus_if;
      logic [31:0] addr;
      logic [31:0] data;
    endinterface

    module TestModule(bus_if bus);
      logic [31:0] temp;

      initial begin
        bus.addr = 32'h1234;
        temp = bus.data;
      end
    endmodule
  )";

  auto index = fixture.CompileSource(code);

  // Test interface port parameter
  fixture.AssertGoToDefinition(*index, code, "bus", 0, 0);

  // Test LHS: bus.addr reference
  fixture.AssertGoToDefinition(*index, code, "addr", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "bus", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "addr", 1, 0);

  // Test RHS: bus.data reference
  fixture.AssertGoToDefinition(*index, code, "data", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "bus", 2, 0);
  fixture.AssertGoToDefinition(*index, code, "data", 1, 0);
}

TEST_CASE(
    "SemanticIndex interface instance in module body works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    interface my_if;
      logic data;
      logic valid;
    endinterface

    module top;
      my_if bus();
      my_if other_bus();

      initial begin
        bus.data = 1'b1;
      end
    endmodule
  )";

  auto index = fixture.CompileSource(code);

  // Test interface type name in body instantiation
  fixture.AssertGoToDefinition(*index, code, "my_if", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "my_if", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "my_if", 2, 0);

  // Test interface instance name self-definition
  fixture.AssertGoToDefinition(*index, code, "bus", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "other_bus", 0, 0);

  // Test member access through body instance
  fixture.AssertGoToDefinition(*index, code, "bus", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "data", 1, 0);
}

TEST_CASE(
    "SemanticIndex unpacked array of interface instances works",
    "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    interface array_if;
      logic [31:0] signal_a;
      logic [31:0] signal_b;
    endinterface

    module top;
      parameter int ARRAY_SIZE = 4;

      array_if if_array[ARRAY_SIZE] ();

      initial begin
        if_array[0].signal_a = 32'h1234;
        if_array[1].signal_b = 32'h5678;
      end
    endmodule
  )";

  auto index = fixture.CompileSource(code);

  // Test interface type name in array instantiation
  fixture.AssertGoToDefinition(*index, code, "array_if", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "signal_a", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "signal_b", 0, 0);

  // Test array instance name self-definition
  fixture.AssertGoToDefinition(*index, code, "if_array", 0, 0);

  // Test array element access and member access
  fixture.AssertGoToDefinition(*index, code, "if_array", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "signal_a", 1, 0);

  fixture.AssertGoToDefinition(*index, code, "if_array", 2, 0);
  fixture.AssertGoToDefinition(*index, code, "signal_b", 1, 0);
}

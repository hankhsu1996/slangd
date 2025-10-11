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

  // Test parameter reference in array dimension
  fixture.AssertGoToDefinition(*index, code, "ARRAY_SIZE", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "ARRAY_SIZE", 1, 0);

  // Test array element access and member access
  fixture.AssertGoToDefinition(*index, code, "if_array", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "signal_a", 1, 0);

  fixture.AssertGoToDefinition(*index, code, "if_array", 2, 0);
  fixture.AssertGoToDefinition(*index, code, "signal_b", 1, 0);
}

TEST_CASE(
    "SemanticIndex interface parameter override references work",
    "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    interface config_if #(
      parameter int FLAG = 0,
      parameter int WIDTH = 8
    ) ();
      logic [WIDTH-1:0] data;
    endinterface

    module top;
      parameter int SIZE = 4;

      // Single instance with parameter override
      config_if #(.FLAG(1)) single_inst ();

      // Array of instances with parameter override
      config_if #(.WIDTH(16)) inst_array [SIZE] ();
    endmodule
  )";

  auto index = fixture.CompileSource(code);

  // Test parameter definitions
  fixture.AssertGoToDefinition(*index, code, "FLAG", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "WIDTH", 0, 0);

  // Test parameter references in single instance override: .FLAG(1)
  fixture.AssertGoToDefinition(*index, code, "FLAG", 1, 0);

  // Test parameter references in array instance override: .WIDTH(16)
  fixture.AssertGoToDefinition(*index, code, "WIDTH", 2, 0);

  // Test array dimension parameter reference
  fixture.AssertGoToDefinition(*index, code, "SIZE", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "SIZE", 1, 0);
}

TEST_CASE(
    "SemanticIndex interface port connection references work", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    interface test_if;
      logic data;
    endinterface

    module child(test_if.master port);
    endmodule

    module parent;
      test_if bus();

      child u_child(
        .port(bus)
      );
    endmodule
  )";

  auto index = fixture.CompileSource(code);

  // Test interface instance declaration
  fixture.AssertGoToDefinition(*index, code, "bus", 0, 0);

  // Test interface instance reference in port connection (RHS)
  // This should jump to the declaration of 'bus' above
  fixture.AssertGoToDefinition(*index, code, "bus", 1, 0);
}

TEST_CASE(
    "SemanticIndex interface array port connection with parameters works",
    "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    interface common_if #(
      parameter int MODE = 0
    ) ();
      logic [31:0] data;
      logic valid;
    endinterface

    module child(
      common_if.master if_port
    );
    endmodule

    module parent;
      parameter int NUM_UNITS = 4;

      common_if #(.MODE(0)) unit_if [NUM_UNITS] ();

      child u_child(
        .if_port(unit_if)
      );
    endmodule
  )";

  auto index = fixture.CompileSource(code);

  // Test interface type name
  fixture.AssertGoToDefinition(*index, code, "common_if", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "common_if", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "common_if", 2, 0);

  // Test parameter definition and reference
  fixture.AssertGoToDefinition(*index, code, "MODE", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "MODE", 1, 0);

  // Test array dimension parameter
  fixture.AssertGoToDefinition(*index, code, "NUM_UNITS", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "NUM_UNITS", 1, 0);

  // Test interface array instance self-definition
  fixture.AssertGoToDefinition(*index, code, "unit_if", 0, 0);

  // Test interface array reference in port connection (no index)
  // This should jump to the array declaration above
  fixture.AssertGoToDefinition(*index, code, "unit_if", 1, 0);
}

TEST_CASE(
    "SemanticIndex interface member array indexing works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    interface mem_if;
      logic [7:0] array_field;
      logic       scalar_field;
    endinterface

    module top;
      parameter int NUM_ITEMS = 4;

      mem_if mem_inst();

      for (genvar idx = 0; idx < NUM_ITEMS; idx++) begin
        assign mem_inst.array_field[idx] = 8'h00;
      end

      assign mem_inst.scalar_field = 1'b1;
    endmodule
  )";

  auto index = fixture.CompileSource(code);

  // Test interface type definition
  fixture.AssertGoToDefinition(*index, code, "mem_if", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "mem_if", 1, 0);

  // Test interface instance self-definition
  fixture.AssertGoToDefinition(*index, code, "mem_inst", 0, 0);

  // Test interface instance reference in member access (with array indexing)
  fixture.AssertGoToDefinition(*index, code, "mem_inst", 1, 0);

  // Test array member field reference (the field being indexed)
  fixture.AssertGoToDefinition(*index, code, "array_field", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "array_field", 1, 0);

  // Test scalar member for comparison
  fixture.AssertGoToDefinition(*index, code, "mem_inst", 2, 0);
  fixture.AssertGoToDefinition(*index, code, "scalar_field", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "scalar_field", 1, 0);
}

TEST_CASE(
    "SemanticIndex interface port passed to submodule works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    interface data_if;
      logic [31:0] data;
      logic valid;
    endinterface

    module child(data_if port);
    endmodule

    module parent(
      data_if external_if
    );
      child u_child(
        .port(external_if)
      );
    endmodule
  )";

  auto index = fixture.CompileSource(code);

  // Test interface type definition
  fixture.AssertGoToDefinition(*index, code, "data_if", 0, 0);

  // Test interface port in parent module declaration
  fixture.AssertGoToDefinition(*index, code, "external_if", 0, 0);

  // Test interface port reference in submodule connection
  // This should resolve to the port declaration (occurrence 0 of "external_if")
  fixture.AssertGoToDefinition(*index, code, "external_if", 1, 0);
}

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

TEST_CASE(
    "SemanticIndex function argument type reference works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module arg_type_test;
      typedef logic [7:0] byte_t;
      typedef struct packed { logic [3:0] addr; } packet_t;

      function void process(byte_t data, packet_t pkt);
      endfunction
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "byte_t", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "byte_t", 1, 0);
  fixture.AssertGoToDefinition(*index, code, "packet_t", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "packet_t", 1, 0);
}

TEST_CASE("SemanticIndex task argument type reference works", "[definition]") {
  SimpleTestFixture fixture;
  std::string code = R"(
    module task_arg_type_test;
      typedef int counter_t;

      task increment(inout counter_t cnt);
        cnt = cnt + 1;
      endtask
    endmodule
  )";

  auto index = fixture.CompileSource(code);
  fixture.AssertGoToDefinition(*index, code, "counter_t", 0, 0);
  fixture.AssertGoToDefinition(*index, code, "counter_t", 1, 0);
}

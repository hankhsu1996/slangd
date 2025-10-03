#include <cstdlib>
#include <string>

#include <asio.hpp>
#include <catch2/catch_all.hpp>
#include <spdlog/spdlog.h>

#include "../../common/async_fixture.hpp"
#include "../test_fixtures.hpp"

constexpr auto kLogLevel = spdlog::level::debug;

auto main(int argc, char* argv[]) -> int {
  spdlog::set_level(kLogLevel);
  spdlog::set_pattern("[%l] %v");

  setenv("TEST_SHARD_INDEX", "0", 0);
  setenv("TEST_TOTAL_SHARDS", "1", 0);
  setenv("TEST_SHARD_STATUS_FILE", "", 0);

  return Catch::Session().run(argc, argv);
}

using slangd::semantic::test::MultiFileSemanticFixture;
using slangd::test::RunAsyncTest;

TEST_CASE("Definition lookup for package imports", "[definition][multifile]") {
  MultiFileSemanticFixture fixture;

  const std::string package_content = R"(
    package test_pkg;
      parameter WIDTH = 32;
      typedef logic [WIDTH-1:0] data_t;
    endpackage
  )";

  const std::string module_content = R"(
    module test_module;
      import test_pkg::*;
      data_t my_data;
    endmodule
  )";

  auto result = fixture.CreateBuilder()
                    .SetCurrentFile(module_content, "test_module")
                    .AddUnopendFile(package_content, "test_pkg")
                    .Build();
  REQUIRE(result.index != nullptr);

  fixture.AssertCrossFileDefinition(*result.index, module_content, "data_t");
}

TEST_CASE(
    "Definition lookup for package name in import statement",
    "[definition][multifile]") {
  MultiFileSemanticFixture fixture;

  const std::string package_content = R"(
    package my_pkg;
      parameter WIDTH = 32;
      typedef logic [WIDTH-1:0] data_t;
    endpackage
  )";

  const std::string module_content = R"(
    module test_module;
      import my_pkg::*;
      data_t my_data;
    endmodule
  )";

  auto result = fixture.CreateBuilder()
                    .SetCurrentFile(module_content, "test_module")
                    .AddUnopendFile(package_content, "my_pkg")
                    .Build();
  REQUIRE(result.index != nullptr);

  fixture.AssertCrossFileDefinition(*result.index, module_content, "my_pkg");
}

TEST_CASE(
    "Definition lookup for cross-file module instantiation",
    "[definition][multifile]") {
  RunAsyncTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    MultiFileSemanticFixture fixture;

    const std::string alu_content = R"(
      module ALU #(parameter WIDTH = 8) (
        input logic [WIDTH-1:0] a, b,
        output logic [WIDTH-1:0] result
      );
      endmodule
    )";

    const std::string top_content = R"(
      module top;
        logic [7:0] x, y, z;
        ALU #(.WIDTH(8)) alu_inst (.a(x), .b(y), .result(z));
      endmodule
    )";

    fixture.CreateFile("alu.sv", alu_content);
    fixture.CreateFile("top.sv", top_content);

    auto result = fixture.BuildSessionFromDiskWithCatalog("top.sv", executor);
    REQUIRE(result.session != nullptr);
    REQUIRE(result.catalog != nullptr);

    MultiFileSemanticFixture::AssertCrossFileDefinition(
        result, "ALU", "top.sv", "alu.sv");

    co_return;
  });
}

TEST_CASE(
    "Definition lookup for same-file module instantiation",
    "[definition][multifile]") {
  MultiFileSemanticFixture fixture;

  const std::string content = R"(
    module counter;
    endmodule

    module top;
      counter cnt_inst;
    endmodule
  )";

  auto result =
      fixture.CreateBuilder().SetCurrentFile(content, "single_file").Build();
  REQUIRE(result.index != nullptr);

  fixture.AssertSameFileDefinition(*result.index, content, "counter");
}

TEST_CASE(
    "Definition lookup for unknown module doesn't crash",
    "[definition][multifile]") {
  MultiFileSemanticFixture fixture;

  const std::string content = R"(
    module top;
      UnknownModule inst;
    endmodule
  )";

  auto result = fixture.CreateBuilder().SetCurrentFile(content, "test").Build();
  REQUIRE(result.index != nullptr);

  fixture.AssertDefinitionNotCrash(*result.index, content, "UnknownModule");
}

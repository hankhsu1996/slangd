#include <cstdlib>

#include <catch2/catch_all.hpp>
#include <spdlog/spdlog.h>

#include "../common/simple_fixture.hpp"

constexpr auto kLogLevel = spdlog::level::warn;

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
    "Parameter definition range should be name only, not full declaration",
    "[definition_extractor]") {
  SimpleTestFixture fixture;

  std::string code = R"(
    module test;
      parameter int WIDTH = 8;
    endmodule
  )";

  auto index = fixture.CompileSource(code);

  // Find the parameter location in the source by searching for the name
  auto param_location = fixture.FindSymbol(code, "WIDTH");
  REQUIRE(param_location.valid());

  // Lookup the definition range
  auto result = SimpleTestFixture::GetDefinitionRange(*index, param_location);

  REQUIRE(result.has_value());

  // The parameter definition range should contain just the parameter name
  // "WIDTH" (5 chars), not the full declaration "WIDTH = 8" (9 chars)
  auto range_length = result->end().offset() - result->start().offset();

  CHECK(range_length == 5);  // Now correctly returns just "WIDTH"
}

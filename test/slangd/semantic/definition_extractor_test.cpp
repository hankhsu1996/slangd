#include <cstdlib>

#include <catch2/catch_all.hpp>
#include <spdlog/spdlog.h>

#include "test_fixtures.hpp"

auto main(int argc, char* argv[]) -> int {
  spdlog::set_level(spdlog::level::debug);
  spdlog::set_pattern("[%l] %v");

  // Suppress Bazel test sharding warnings
  setenv("TEST_SHARD_INDEX", "0", 0);
  setenv("TEST_TOTAL_SHARDS", "1", 0);
  setenv("TEST_SHARD_STATUS_FILE", "", 0);

  return Catch::Session().run(argc, argv);
}

using slangd::semantic::test::SemanticTestFixture;

TEST_CASE(
    "Parameter definition range should be name only, not full declaration",
    "[definition_extractor]") {
  SemanticTestFixture fixture;

  std::string code = R"(
    module test;
      parameter int WIDTH = 8;
    endmodule
  )";

  auto index = fixture.BuildIndexFromSource(code);

  // Find the parameter location in the source by searching for the name
  auto param_location = fixture.FindLocation(code, "WIDTH");
  REQUIRE(param_location.valid());

  // Lookup the definition range
  auto result = index->LookupDefinitionAt(param_location);
  REQUIRE(result.has_value());

  // For our parameter `parameter int WIDTH = 8;`, we want the range to be just
  // "WIDTH" (5 chars) But currently it probably includes "WIDTH = 8" (9 chars)
  auto range_length = result->end().offset() - result->start().offset();

  // This test should FAIL initially to show current behavior
  CHECK(range_length == 5);  // Just "WIDTH"
}

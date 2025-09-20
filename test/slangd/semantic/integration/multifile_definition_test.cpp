#include <cstdlib>
#include <string>

#include <asio.hpp>
#include <catch2/catch_all.hpp>
#include <spdlog/spdlog.h>

#include "../test_fixtures.hpp"

// Set to spdlog::level::info when debugging, spdlog::level::warn otherwise
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

using slangd::semantic::test::AsyncMultiFileFixture;

TEST_CASE("Definition lookup for package imports", "[definition][multifile]") {
  AsyncMultiFileFixture fixture;

  // Create package file with typedef
  const std::string package_content = R"(
    package test_pkg;
      parameter WIDTH = 32;
      typedef logic [WIDTH-1:0] data_t;
    endpackage
  )";

  // Create module that imports and uses the package type
  const std::string module_content = R"(
    module test_module;
      import test_pkg::*;
      data_t my_data;  // Should resolve to package typedef
    endmodule
  )";

  // Build SemanticIndex using semantic-layer testing
  auto result = fixture.CreateBuilder()
                    .SetCurrentFile(module_content, "test_module")
                    .AddUnopendFile(package_content, "test_pkg")
                    .Build();
  REQUIRE(result.index != nullptr);

  // Find the data_t reference in the module
  auto location = fixture.FindLocation(module_content, "data_t");
  REQUIRE(location.valid());

  // Look up definition at that location - this tests actual cross-file
  // definition resolution
  auto def_range = result.index->LookupDefinitionAt(location);
  REQUIRE(def_range.has_value());

  // Verify cross-file definition resolution works
  REQUIRE(def_range->start().buffer() != location.buffer());
}

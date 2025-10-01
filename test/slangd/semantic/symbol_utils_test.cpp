#include "slangd/semantic/symbol_utils.hpp"

#include <cstdlib>
#include <memory>
#include <string>

#include <catch2/catch_all.hpp>
#include <slang/ast/Compilation.h>
#include <slang/ast/Symbol.h>
#include <slang/syntax/SyntaxTree.h>
#include <spdlog/spdlog.h>

#include "../common/simple_fixture.hpp"

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

using slangd::semantic::ComputeLspRange;
using slangd::test::SimpleTestFixture;

TEST_CASE(
    "ComputeLspRange handles symbols without locations", "[symbol_utils]") {
  auto source_manager = std::make_shared<slang::SourceManager>();

  // Create a mock symbol without location for edge case testing
  slang::Bag options;
  auto compilation = std::make_unique<slang::ast::Compilation>(options);

  const auto& root = compilation->getRoot();

  // Test that ComputeLspRange doesn't crash with symbols without location
  auto range = ComputeLspRange(root, *source_manager);

  // Should return zero range for symbols without location
  REQUIRE(range.start.line == 0);
  REQUIRE(range.start.character == 0);
  REQUIRE(range.end.line == 0);
  REQUIRE(range.end.character == 0);
}

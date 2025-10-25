#include "slangd/semantic/symbol_utils.hpp"

#include <cstdlib>
#include <memory>
#include <string>

#include <catch2/catch_all.hpp>
#include <slang/ast/Compilation.h>
#include <slang/ast/Symbol.h>
#include <slang/ast/symbols/CompilationUnitSymbols.h>
#include <slang/syntax/SyntaxTree.h>
#include <spdlog/spdlog.h>

#include "slangd/utils/conversion.hpp"

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

using slangd::CreateSymbolRange;

TEST_CASE(
    "CreateSymbolRange handles symbols without locations", "[symbol_utils]") {
  // Create a mock symbol without location for edge case testing
  slang::Bag options;
  auto compilation = std::make_unique<slang::ast::Compilation>(options);

  // Use base class reference - CreateSymbolRange expects Symbol&
  const slang::ast::Symbol& root = compilation->getRoot();

  // Test that CreateSymbolRange doesn't crash with symbols without location
  // It automatically derives SourceManager from symbol's compilation
  // FAIL-FAST: Returns nullopt instead of zero range
  auto range_opt = CreateSymbolRange(root);

  // Should return nullopt for symbols without location
  REQUIRE(!range_opt.has_value());
}

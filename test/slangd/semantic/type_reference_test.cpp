#include <cstdlib>
#include <string>

#include <catch2/catch_all.hpp>
#include <slang/ast/Symbol.h>
#include <slang/util/Enum.h>
#include <spdlog/spdlog.h>

#include "../common/semantic_fixture.hpp"

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

using Fixture = slangd::test::SemanticTestFixture;

// TypeReferenceSymbol is a wrapper type created in LSP mode to preserve typedef
// usage locations for go-to-definition. These tests ensure TypeReferenceSymbol
// properly delegates type system methods to the wrapped type without breaking
// normal Slang compilation behavior.

TEST_CASE(
    "TypeReferenceSymbol nested typedef in binary expression",
    "[type_reference][regression]") {
  std::string code = R"(
    module test;
      typedef struct packed {
        logic [7:0] field_a;
        logic [7:0] field_b;
      } data_t;

      function automatic data_t compute(data_t input_val);
        typedef data_t local_t;
        return input_val - local_t'(1);
      endfunction
    endmodule
  )";

  // Bug: Nested typedef (local_t is typedef of data_t) creates
  // TypeReferenceSymbol that fails isIntegral()/isNumeric() checks in binary
  // expressions Expected: Should compile without BadBinaryExpression error
  // Left: input_val (data_t), Right: local_t'(1) (TypeReferenceSymbol ->
  // TypeAlias -> data_t)

  // BuildIndex throws if there are any compilation errors
  // This test will fail if BadBinaryExpression error occurs
  REQUIRE_NOTHROW(Fixture::BuildIndex(code));
}

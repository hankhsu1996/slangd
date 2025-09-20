#include "slangd/core/project_layout_service.hpp"

#include <string>

#include <asio.hpp>
#include <catch2/catch_all.hpp>
#include <spdlog/spdlog.h>

#include "slangd/utils/canonical_path.hpp"

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

TEST_CASE(
    "ConfigManager RebuildLayout increments version", "[layout_service]") {
  asio::io_context io_context;
  auto executor = io_context.get_executor();
  auto workspace_root = slangd::CanonicalPath::CurrentPath();

  auto layout_service =
      slangd::ProjectLayoutService::Create(executor, workspace_root);

  // Get initial version
  uint64_t initial_version = layout_service->GetLayoutVersion();

  // Call RebuildLayout
  layout_service->RebuildLayout();

  // Verify version incremented
  uint64_t new_version = layout_service->GetLayoutVersion();
  REQUIRE(new_version == initial_version + 1);

  // Call RebuildLayout again
  layout_service->RebuildLayout();

  // Verify version incremented again
  uint64_t final_version = layout_service->GetLayoutVersion();
  REQUIRE(final_version == initial_version + 2);
}

#include "slangd/core/config_manager.hpp"

#include <string>

#include <asio.hpp>
#include <catch2/catch_all.hpp>
#include <spdlog/spdlog.h>

#include "slangd/utils/canonical_path.hpp"

auto main(int argc, char* argv[]) -> int {
  spdlog::set_level(spdlog::level::debug);
  spdlog::set_pattern("[%l] %v");
  return Catch::Session().run(argc, argv);
}

TEST_CASE(
    "ConfigManager RebuildLayout increments version", "[config_manager]") {
  asio::io_context io_context;
  auto executor = io_context.get_executor();
  auto workspace_root = slangd::CanonicalPath::CurrentPath();

  auto config_manager = slangd::ConfigManager::Create(executor, workspace_root);

  // Get initial version
  uint64_t initial_version = config_manager->GetLayoutVersion();

  // Call RebuildLayout
  config_manager->RebuildLayout();

  // Verify version incremented
  uint64_t new_version = config_manager->GetLayoutVersion();
  REQUIRE(new_version == initial_version + 1);

  // Call RebuildLayout again
  config_manager->RebuildLayout();

  // Verify version incremented again
  uint64_t final_version = config_manager->GetLayoutVersion();
  REQUIRE(final_version == initial_version + 2);
}

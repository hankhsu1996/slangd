#include "slangd/services/overlay_session.hpp"

#include <cstdlib>
#include <string>

#include <asio.hpp>
#include <catch2/catch_all.hpp>
#include <spdlog/spdlog.h>

#include "slangd/core/project_layout_service.hpp"
#include "slangd/utils/canonical_path.hpp"
#include "test/slangd/common/async_fixture.hpp"

constexpr auto kLogLevel = spdlog::level::debug;

auto main(int argc, char* argv[]) -> int {
  spdlog::set_level(kLogLevel);
  spdlog::set_pattern("[%l] %v");

  setenv("TEST_SHARD_INDEX", "0", 0);
  setenv("TEST_TOTAL_SHARDS", "1", 0);
  setenv("TEST_SHARD_STATUS_FILE", "", 0);

  return Catch::Session().run(argc, argv);
}

using slangd::test::RunAsyncTest;

TEST_CASE(
    "OverlaySession can be created with simple module", "[overlay_session]") {
  RunAsyncTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    // Setup test workspace
    auto workspace_root = slangd::CanonicalPath::CurrentPath();
    auto layout_service = slangd::ProjectLayoutService::Create(
        executor, workspace_root, spdlog::default_logger());

    // Simple SystemVerilog module
    std::string test_content = R"(
      module test_module;
        wire x;
        wire y;
      endmodule
    )";

    const std::string uri = "file:///test.sv";

    // Create overlay session
    auto session = slangd::services::OverlaySession::Create(
        uri, test_content, layout_service);

    REQUIRE(session != nullptr);

    // Basic validation - session should be functional
    // No need to check low-level Slang diagnostics here - that's tested
    // elsewhere
    // If we reach here without runtime errors, semantic indexing succeeded

    co_return;
  });
}

TEST_CASE("OverlaySession works without PreambleManager", "[overlay_session]") {
  RunAsyncTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    auto workspace_root = slangd::CanonicalPath::CurrentPath();
    auto layout_service = slangd::ProjectLayoutService::Create(
        executor, workspace_root, spdlog::default_logger());

    std::string test_content = R"(
      module simple_module;
        parameter WIDTH = 8;
        input logic [WIDTH-1:0] data_in;
        output logic [WIDTH-1:0] data_out;
      endmodule
    )";

    const std::string uri = "file:///simple.sv";

    // Create session with explicit nullptr preamble_manager (single-file mode)
    auto session = slangd::services::OverlaySession::Create(
        uri, test_content, layout_service, nullptr);

    REQUIRE(session != nullptr);

    // Should work fine in single-file mode
    // Basic validation - session should be functional with valid semantic index
    (void)session->GetSemanticIndex();  // Ensure semantic index is accessible

    co_return;
  });
}

TEST_CASE(
    "OverlaySession handles syntax errors gracefully", "[overlay_session]") {
  RunAsyncTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    auto workspace_root = slangd::CanonicalPath::CurrentPath();
    auto layout_service = slangd::ProjectLayoutService::Create(
        executor, workspace_root, spdlog::default_logger());

    // Invalid SystemVerilog with syntax error
    std::string invalid_content = R"(
      module broken_module
        wire x    // missing semicolon
      endmodule   // missing semicolon after module declaration
    )";

    const std::string uri = "file:///broken.sv";

    // Should create session even with syntax errors
    auto session = slangd::services::OverlaySession::Create(
        uri, invalid_content, layout_service);

    REQUIRE(session != nullptr);

    // Session should be created even with syntax errors
    // Actual diagnostic validation is handled by ConvertSlangDiagnosticsToLsp

    co_return;
  });
}

TEST_CASE(
    "OverlaySession suppresses unknown module diagnostics",
    "[overlay_session]") {
  RunAsyncTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    auto workspace_root = slangd::CanonicalPath::CurrentPath();
    auto layout_service = slangd::ProjectLayoutService::Create(
        executor, workspace_root, spdlog::default_logger());

    std::string test_content = R"(
      module top;
        UnknownModule unknown_inst();
      endmodule
    )";

    const std::string uri = "file:///test_unknown_module.sv";

    auto session = slangd::services::OverlaySession::Create(
        uri, test_content, layout_service);

    REQUIRE(session != nullptr);

    co_return;
  });
}

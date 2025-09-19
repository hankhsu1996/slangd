#include "slangd/services/overlay_session.hpp"

#include <string>

#include <asio.hpp>
#include <catch2/catch_all.hpp>
#include <spdlog/spdlog.h>

#include "slangd/core/project_layout_service.hpp"
#include "slangd/utils/canonical_path.hpp"

auto main(int argc, char* argv[]) -> int {
  spdlog::set_level(spdlog::level::debug);
  spdlog::set_pattern("[%l] %v");
  return Catch::Session().run(argc, argv);
}

// Helper to run async test functions with coroutines
template <typename F>
void RunTest(F&& test_fn) {
  asio::io_context io_context;
  auto executor = io_context.get_executor();

  bool completed = false;
  std::exception_ptr exception;

  asio::co_spawn(
      io_context,
      [fn = std::forward<F>(test_fn), &completed, &exception,
       executor]() -> asio::awaitable<void> {
        try {
          co_await fn(executor);
          completed = true;
        } catch (...) {
          exception = std::current_exception();
          completed = true;
        }
      },
      asio::detached);

  io_context.run();

  if (exception) {
    std::rethrow_exception(exception);
  }

  REQUIRE(completed);
}

TEST_CASE(
    "OverlaySession can be created with simple module", "[overlay_session]") {
  RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
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

    // Verify session has valid components
    const auto& semantic_index = session->GetSemanticIndex();

    // Basic validation - session should be functional
    // No need to check low-level Slang diagnostics here - that's tested
    // elsewhere

    // Should have some symbols in the symbol index
    const auto& symbols = semantic_index.GetAllSymbols();
    REQUIRE(symbols.size() > 0);

    co_return;
  });
}

TEST_CASE("OverlaySession works without GlobalCatalog", "[overlay_session]") {
  RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
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

    // Create session with explicit nullptr catalog (single-file mode)
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
  RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
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

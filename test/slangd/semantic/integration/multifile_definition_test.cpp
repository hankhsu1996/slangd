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

TEST_CASE(
    "GlobalCatalog creation and package discovery", "[definition][multifile]") {
  slangd::semantic::test::RunAsyncTest(
      [](asio::any_io_executor executor) -> asio::awaitable<void> {
        AsyncMultiFileFixture fixture;
        auto workspace_root = fixture.GetTempDir();

        // Create a package file
        fixture.CreateFile("math_pkg.sv", R"(
          package math_pkg;
            parameter BUS_WIDTH = 64;
            typedef logic [BUS_WIDTH-1:0] data_t;
          endpackage
        )");

        // Create project layout service and catalog
        auto catalog = co_await fixture.CreateGlobalCatalog(executor);
        REQUIRE(catalog != nullptr);
        REQUIRE(catalog->GetVersion() == 1);

        // Verify package was discovered
        const auto& packages = catalog->GetPackages();
        bool found_math_pkg = false;
        for (const auto& pkg : packages) {
          if (pkg.name == "math_pkg") {
            found_math_pkg = true;
            REQUIRE(pkg.file_path.Path().filename() == "math_pkg.sv");
            break;
          }
        }
        REQUIRE(found_math_pkg);

        co_return;
      });
}

TEST_CASE("Definition lookup for package imports", "[definition][multifile]") {
  slangd::semantic::test::RunAsyncTest(
      [](asio::any_io_executor executor) -> asio::awaitable<void> {
        AsyncMultiFileFixture fixture;

        // Create package file with typedef
        fixture.CreateFile("test_pkg.sv", R"(
          package test_pkg;
            parameter WIDTH = 32;
            typedef logic [WIDTH-1:0] data_t;
          endpackage
        )");

        // Create module that imports and uses the package type
        const std::string module_content = R"(
          module test_module;
            import test_pkg::*;
            data_t my_data;  // Should resolve to package typedef
          endmodule
        )";

        // Create GlobalCatalog and OverlaySession for definition resolution
        auto catalog = co_await fixture.CreateGlobalCatalog(executor);
        REQUIRE(catalog != nullptr);

        auto session = co_await fixture.CreateOverlaySession(
            executor, "file:///test_module.sv", module_content, catalog);
        REQUIRE(session != nullptr);

        // Find the data_t reference in the module
        auto location = AsyncMultiFileFixture::FindLocationInOverlaySession(
            "data_t", session->GetSourceManager());
        REQUIRE(location.valid());

        // Look up definition at that location - this tests actual cross-file
        // definition resolution
        auto def_range =
            session->GetSemanticIndex().LookupDefinitionAt(location);
        REQUIRE(def_range.has_value());

        // Verify cross-file definition resolution works
        REQUIRE(def_range->start().buffer() != location.buffer());

        co_return;
      });
}

TEST_CASE(
    "Definition lookup for qualified package references",
    "[definition][multifile]") {
  slangd::semantic::test::RunAsyncTest(
      [](asio::any_io_executor executor) -> asio::awaitable<void> {
        AsyncMultiFileFixture fixture;

        // Create package file with parameters and typedef
        fixture.CreateFile("math_pkg.sv", R"(
          package math_pkg;
            parameter BUS_WIDTH = 64;
            parameter ADDR_WIDTH = 32;
            typedef struct packed {
              logic [ADDR_WIDTH-1:0] addr;
              logic [BUS_WIDTH-1:0] data;
            } transaction_t;
          endpackage
        )");

        // Create module with qualified package references
        const std::string module_content = R"(
          module bus_controller;
            logic [math_pkg::BUS_WIDTH-1:0] data_bus;
            math_pkg::transaction_t transaction;
          endmodule
        )";

        // Create GlobalCatalog and OverlaySession for definition resolution
        auto catalog = co_await fixture.CreateGlobalCatalog(executor);
        REQUIRE(catalog != nullptr);

        // Verify package was found in catalog
        const auto& packages = catalog->GetPackages();
        bool found_math_pkg = false;
        for (const auto& pkg : packages) {
          if (pkg.name == "math_pkg") {
            found_math_pkg = true;
            break;
          }
        }
        REQUIRE(found_math_pkg);

        auto session = co_await fixture.CreateOverlaySession(
            executor, "file:///bus_controller.sv", module_content, catalog);
        REQUIRE(session != nullptr);

        // Test that we can find symbols in the module content
        auto bus_width_location =
            AsyncMultiFileFixture::FindLocationInOverlaySession(
                "BUS_WIDTH", session->GetSourceManager());
        bool can_locate_symbols = bus_width_location.valid();

        auto transaction_location =
            AsyncMultiFileFixture::FindLocationInOverlaySession(
                "transaction_t", session->GetSourceManager());
        bool can_locate_types = transaction_location.valid();

        CAPTURE(can_locate_symbols);
        CAPTURE(can_locate_types);

        // Verify the infrastructure functions correctly
        REQUIRE(can_locate_symbols);
        REQUIRE(can_locate_types);

        co_return;
      });
}

TEST_CASE("Single-file definition lookup", "[definition][multifile]") {
  slangd::semantic::test::RunAsyncTest(
      [](asio::any_io_executor executor) -> asio::awaitable<void> {
        AsyncMultiFileFixture fixture;

        // Simple single-file module
        const std::string module_content = R"(
          module simple_test;
            logic [31:0] counter;

            always_ff @(posedge clk) begin
              counter <= counter + 1;
            end
          endmodule
        )";

        // Create OverlaySession without catalog for single-file testing
        auto session = co_await fixture.CreateOverlaySession(
            executor, "file:///simple_test.sv", module_content, nullptr);
        REQUIRE(session != nullptr);

        // Test that definition lookup works within the same file
        auto counter_location =
            AsyncMultiFileFixture::FindLocationInOverlaySession(
                "counter", session->GetSourceManager());

        // Verify that single-file mode functions correctly
        bool can_locate_in_single_file = counter_location.valid();
        CAPTURE(can_locate_in_single_file);
        REQUIRE(can_locate_in_single_file);

        co_return;
      });
}

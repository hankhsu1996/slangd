#include "slangd/services/preamble_manager.hpp"

#include <cstdlib>
#include <fstream>
#include <string>
#include <string_view>

#include <asio.hpp>
#include <catch2/catch_all.hpp>
#include <slang/ast/symbols/CompilationUnitSymbols.h>
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

// Helper to create test files in temporary directory
class PreambleManagerTestFixture {
 public:
  PreambleManagerTestFixture() {
    // Create a temporary directory for test files
    temp_dir_ =
        std::filesystem::temp_directory_path() / "slangd_preamble_manager_test";
    std::filesystem::create_directories(temp_dir_);
  }

  ~PreambleManagerTestFixture() {
    // Clean up test files
    std::error_code ec;
    std::filesystem::remove_all(temp_dir_, ec);
  }

  // Explicitly delete copy and move operations (not needed for this test
  // fixture)
  PreambleManagerTestFixture(const PreambleManagerTestFixture&) = delete;
  auto operator=(const PreambleManagerTestFixture&)
      -> PreambleManagerTestFixture& = delete;
  PreambleManagerTestFixture(PreambleManagerTestFixture&&) = delete;
  auto operator=(PreambleManagerTestFixture&&)
      -> PreambleManagerTestFixture& = delete;

  auto CreateFile(std::string_view filename, std::string_view content) const
      -> slangd::CanonicalPath {
    auto file_path = temp_dir_ / filename;
    std::ofstream file(file_path);
    file << content;
    file.close();
    return slangd::CanonicalPath(file_path);
  }

  [[nodiscard]] auto BuildPreambleManager(asio::any_io_executor executor) const
      -> std::shared_ptr<slangd::services::PreambleManager> {
    auto layout_service = slangd::ProjectLayoutService::Create(
        executor, slangd::CanonicalPath(temp_dir_), spdlog::default_logger());
    return slangd::services::PreambleManager::CreateFromProjectLayout(
        layout_service, spdlog::default_logger());
  }

  static void AssertModuleExists(
      const slangd::services::PreambleManager& preamble_manager,
      std::string_view name, std::string_view expected_filename) {
    const auto& modules = preamble_manager.GetModules();
    auto it = modules.find(std::string(name));
    if (it != modules.end()) {
      REQUIRE(it->second.file_path.Path().filename() == expected_filename);
      return;
    }
    FAIL("Module '" << name << "' not found");
  }

  static void AssertPackageExists(
      const slangd::services::PreambleManager& preamble_manager,
      std::string_view name, std::string_view expected_filename) {
    const auto& packages = preamble_manager.GetPackages();
    auto it = packages.find(std::string(name));
    if (it != packages.end()) {
      REQUIRE(it->second.file_path.Path().filename() == expected_filename);
      return;
    }
    FAIL("Package '" << name << "' not found");
  }

  static void AssertInterfaceExists(
      const slangd::services::PreambleManager& preamble_manager,
      std::string_view name, std::string_view expected_filename) {
    const auto& interfaces = preamble_manager.GetInterfaces();
    auto it = interfaces.find(std::string(name));
    if (it != interfaces.end()) {
      REQUIRE(it->second.file_path.Path().filename() == expected_filename);
      return;
    }
    FAIL("Interface '" << name << "' not found");
  }

 private:
  std::filesystem::path temp_dir_;
};

TEST_CASE("PreambleManager package discovery", "[preamble_manager]") {
  RunAsyncTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    PreambleManagerTestFixture fixture;
    fixture.CreateFile("math_pkg.sv", R"(
      package math_pkg;
        parameter BUS_WIDTH = 64;
        typedef logic [BUS_WIDTH-1:0] data_t;
      endpackage
    )");

    auto preamble_manager = fixture.BuildPreambleManager(executor);

    REQUIRE(preamble_manager != nullptr);
    REQUIRE(preamble_manager->GetVersion() == 1);
    PreambleManagerTestFixture::AssertPackageExists(
        *preamble_manager, "math_pkg", "math_pkg.sv");

    co_return;
  });
}

TEST_CASE("PreambleManager interface discovery", "[preamble_manager]") {
  RunAsyncTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    PreambleManagerTestFixture fixture;
    fixture.CreateFile("test_interface.sv", R"(
      interface test_interface;
        logic [7:0] data;
        logic valid;
        modport producer (output data, valid);
        modport consumer (input data, valid);
      endinterface
    )");

    auto preamble_manager = fixture.BuildPreambleManager(executor);

    REQUIRE(preamble_manager != nullptr);
    REQUIRE(preamble_manager->GetVersion() == 1);
    PreambleManagerTestFixture::AssertInterfaceExists(
        *preamble_manager, "test_interface", "test_interface.sv");

    co_return;
  });
}

TEST_CASE("PreambleManager mixed content discovery", "[preamble_manager]") {
  RunAsyncTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    PreambleManagerTestFixture fixture;
    fixture.CreateFile("types_pkg.sv", R"(
      package types_pkg;
        typedef logic [31:0] word_t;
      endpackage
    )");

    fixture.CreateFile("bus_interface.sv", R"(
      interface bus_interface;
        logic clk;
        logic rst;
        modport master (output clk, rst);
      endinterface
    )");

    fixture.CreateFile("top_module.sv", R"(
      module top_module;
        logic clk;
      endmodule
    )");

    auto preamble_manager = fixture.BuildPreambleManager(executor);

    REQUIRE(preamble_manager != nullptr);
    REQUIRE(preamble_manager->GetPackages().size() == 2);
    REQUIRE(preamble_manager->GetInterfaces().size() == 1);

    PreambleManagerTestFixture::AssertPackageExists(
        *preamble_manager, "types_pkg", "types_pkg.sv");
    PreambleManagerTestFixture::AssertInterfaceExists(
        *preamble_manager, "bus_interface", "bus_interface.sv");

    co_return;
  });
}

TEST_CASE("PreambleManager module discovery", "[preamble_manager]") {
  RunAsyncTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    PreambleManagerTestFixture fixture;
    fixture.CreateFile("alu_module.sv", R"(
      module ALU #(parameter WIDTH = 8) (
        input logic [WIDTH-1:0] a,
        input logic [WIDTH-1:0] b,
        output logic [WIDTH-1:0] result
      );
        assign result = a + b;
      endmodule
    )");

    auto preamble_manager = fixture.BuildPreambleManager(executor);
    REQUIRE(preamble_manager != nullptr);
    PreambleManagerTestFixture::AssertModuleExists(
        *preamble_manager, "ALU", "alu_module.sv");

    co_return;
  });
}

TEST_CASE("PreambleManager package symbol storage", "[preamble_manager]") {
  RunAsyncTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    PreambleManagerTestFixture fixture;
    fixture.CreateFile("config_pkg.sv", R"(
      package config_pkg;
        parameter WIDTH = 32;
        parameter DEPTH = 16;
        typedef logic [WIDTH-1:0] word_t;
      endpackage
    )");

    auto preamble_manager = fixture.BuildPreambleManager(executor);

    // Test GetPackage() API
    const auto* pkg = preamble_manager->GetPackage("config_pkg");
    REQUIRE(pkg != nullptr);
    REQUIRE(pkg->name == "config_pkg");

    // Test nonexistent package
    const auto* nonexistent = preamble_manager->GetPackage("nonexistent");
    REQUIRE(nonexistent == nullptr);

    co_return;
  });
}

// NOTE: GetSymbolInfo tests removed - we now use lazy on-demand conversion
// via CreateSymbolLspLocation() instead of pre-computing symbol locations.
// Cross-file resolution is tested in package_preamble_test.cpp

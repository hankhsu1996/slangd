#include "slangd/services/global_catalog.hpp"

#include <cstdlib>
#include <fstream>
#include <string>
#include <string_view>

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

// Helper to create test files in temporary directory
class GlobalCatalogTestFixture {
 public:
  GlobalCatalogTestFixture() {
    // Create a temporary directory for test files
    temp_dir_ =
        std::filesystem::temp_directory_path() / "slangd_global_catalog_test";
    std::filesystem::create_directories(temp_dir_);
  }

  ~GlobalCatalogTestFixture() {
    // Clean up test files
    std::error_code ec;
    std::filesystem::remove_all(temp_dir_, ec);
  }

  // Explicitly delete copy and move operations (not needed for this test
  // fixture)
  GlobalCatalogTestFixture(const GlobalCatalogTestFixture&) = delete;
  auto operator=(const GlobalCatalogTestFixture&)
      -> GlobalCatalogTestFixture& = delete;
  GlobalCatalogTestFixture(GlobalCatalogTestFixture&&) = delete;
  auto operator=(GlobalCatalogTestFixture&&)
      -> GlobalCatalogTestFixture& = delete;

  auto CreateFile(std::string_view filename, std::string_view content) const
      -> slangd::CanonicalPath {
    auto file_path = temp_dir_ / filename;
    std::ofstream file(file_path);
    file << content;
    file.close();
    return slangd::CanonicalPath(file_path);
  }

  [[nodiscard]] auto BuildCatalog(asio::any_io_executor executor) const
      -> std::shared_ptr<slangd::services::GlobalCatalog> {
    auto layout_service = slangd::ProjectLayoutService::Create(
        executor, slangd::CanonicalPath(temp_dir_), spdlog::default_logger());
    return slangd::services::GlobalCatalog::CreateFromProjectLayout(
        layout_service, spdlog::default_logger());
  }

  static void AssertModuleExists(
      const slangd::services::GlobalCatalog& catalog, std::string_view name,
      std::string_view expected_filename) {
    const auto& modules = catalog.GetModules();
    for (const auto& mod : modules) {
      if (mod.name == name) {
        REQUIRE(mod.file_path.Path().filename() == expected_filename);
        REQUIRE(mod.definition_range.start().valid());
        return;
      }
    }
    FAIL("Module '" << name << "' not found");
  }

  static void AssertPackageExists(
      const slangd::services::GlobalCatalog& catalog, std::string_view name,
      std::string_view expected_filename) {
    const auto& packages = catalog.GetPackages();
    for (const auto& pkg : packages) {
      if (pkg.name == name) {
        REQUIRE(pkg.file_path.Path().filename() == expected_filename);
        return;
      }
    }
    FAIL("Package '" << name << "' not found");
  }

  static void AssertInterfaceExists(
      const slangd::services::GlobalCatalog& catalog, std::string_view name,
      std::string_view expected_filename) {
    const auto& interfaces = catalog.GetInterfaces();
    for (const auto& iface : interfaces) {
      if (iface.name == name) {
        REQUIRE(iface.file_path.Path().filename() == expected_filename);
        return;
      }
    }
    FAIL("Interface '" << name << "' not found");
  }

  static void AssertParameterExists(
      const slangd::services::ModuleInfo& module, std::string_view param_name) {
    for (const auto& param : module.parameters) {
      if (param.name == param_name) {
        REQUIRE(param.def_range.start().valid());
        return;
      }
    }
    FAIL(
        "Parameter '" << param_name << "' not found in module '" << module.name
                      << "'");
  }

  static void AssertPortExists(
      const slangd::services::ModuleInfo& module, std::string_view port_name) {
    for (const auto& port : module.ports) {
      if (port.name == port_name) {
        REQUIRE(port.def_range.start().valid());
        return;
      }
    }
    FAIL(
        "Port '" << port_name << "' not found in module '" << module.name
                 << "'");
  }

 private:
  std::filesystem::path temp_dir_;
};

TEST_CASE("GlobalCatalog package discovery", "[global_catalog]") {
  RunAsyncTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    GlobalCatalogTestFixture fixture;
    fixture.CreateFile("math_pkg.sv", R"(
      package math_pkg;
        parameter BUS_WIDTH = 64;
        typedef logic [BUS_WIDTH-1:0] data_t;
      endpackage
    )");

    auto catalog = fixture.BuildCatalog(executor);

    REQUIRE(catalog != nullptr);
    REQUIRE(catalog->GetVersion() == 1);
    GlobalCatalogTestFixture::AssertPackageExists(
        *catalog, "math_pkg", "math_pkg.sv");

    co_return;
  });
}

TEST_CASE("GlobalCatalog interface discovery", "[global_catalog]") {
  RunAsyncTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    GlobalCatalogTestFixture fixture;
    fixture.CreateFile("test_interface.sv", R"(
      interface test_interface;
        logic [7:0] data;
        logic valid;
        modport producer (output data, valid);
        modport consumer (input data, valid);
      endinterface
    )");

    auto catalog = fixture.BuildCatalog(executor);

    REQUIRE(catalog != nullptr);
    REQUIRE(catalog->GetVersion() == 1);
    GlobalCatalogTestFixture::AssertInterfaceExists(
        *catalog, "test_interface", "test_interface.sv");

    co_return;
  });
}

TEST_CASE("GlobalCatalog mixed content discovery", "[global_catalog]") {
  RunAsyncTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    GlobalCatalogTestFixture fixture;
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

    auto catalog = fixture.BuildCatalog(executor);

    REQUIRE(catalog != nullptr);
    REQUIRE(catalog->GetPackages().size() == 2);
    REQUIRE(catalog->GetInterfaces().size() == 1);

    GlobalCatalogTestFixture::AssertPackageExists(
        *catalog, "types_pkg", "types_pkg.sv");
    GlobalCatalogTestFixture::AssertInterfaceExists(
        *catalog, "bus_interface", "bus_interface.sv");

    co_return;
  });
}

TEST_CASE("GlobalCatalog module discovery", "[global_catalog]") {
  RunAsyncTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    GlobalCatalogTestFixture fixture;
    fixture.CreateFile("alu_module.sv", R"(
      module ALU #(parameter WIDTH = 8) (
        input logic [WIDTH-1:0] a,
        input logic [WIDTH-1:0] b,
        output logic [WIDTH-1:0] result
      );
        assign result = a + b;
      endmodule
    )");

    auto catalog = fixture.BuildCatalog(executor);
    REQUIRE(catalog != nullptr);
    GlobalCatalogTestFixture::AssertModuleExists(
        *catalog, "ALU", "alu_module.sv");

    co_return;
  });
}

TEST_CASE("GlobalCatalog module parameter extraction", "[global_catalog]") {
  RunAsyncTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    GlobalCatalogTestFixture fixture;
    fixture.CreateFile("fifo_module.sv", R"(
      module FIFO #(
        parameter DEPTH = 16,
        parameter WIDTH = 32
      ) (
        input logic clk,
        input logic [WIDTH-1:0] data_in,
        output logic [WIDTH-1:0] data_out
      );
      endmodule
    )");

    auto catalog = fixture.BuildCatalog(executor);
    const auto* fifo_module = catalog->GetModule("FIFO");
    REQUIRE(fifo_module != nullptr);
    REQUIRE(fifo_module->parameters.size() == 2);

    GlobalCatalogTestFixture::AssertParameterExists(*fifo_module, "DEPTH");
    GlobalCatalogTestFixture::AssertParameterExists(*fifo_module, "WIDTH");

    co_return;
  });
}

TEST_CASE("GlobalCatalog module port extraction", "[global_catalog]") {
  RunAsyncTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    GlobalCatalogTestFixture fixture;
    fixture.CreateFile("register_module.sv", R"(
      module Register (
        input logic clk,
        input logic reset,
        input logic [7:0] data_in,
        output logic [7:0] data_out
      );
      endmodule
    )");

    auto catalog = fixture.BuildCatalog(executor);
    const auto* register_module = catalog->GetModule("Register");
    REQUIRE(register_module != nullptr);
    REQUIRE(register_module->ports.size() == 4);

    GlobalCatalogTestFixture::AssertPortExists(*register_module, "clk");
    GlobalCatalogTestFixture::AssertPortExists(*register_module, "reset");
    GlobalCatalogTestFixture::AssertPortExists(*register_module, "data_in");
    GlobalCatalogTestFixture::AssertPortExists(*register_module, "data_out");

    co_return;
  });
}

TEST_CASE("GlobalCatalog GetModule lookup", "[global_catalog]") {
  RunAsyncTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    GlobalCatalogTestFixture fixture;
    fixture.CreateFile("counter.sv", R"(
      module Counter (
        input logic clk,
        output logic [7:0] count
      );
      endmodule
    )");

    fixture.CreateFile("timer.sv", R"(
      module Timer (
        input logic clk,
        input logic reset
      );
      endmodule
    )");

    auto catalog = fixture.BuildCatalog(executor);

    const auto* counter = catalog->GetModule("Counter");
    REQUIRE(counter != nullptr);
    REQUIRE(counter->name == "Counter");

    const auto* timer = catalog->GetModule("Timer");
    REQUIRE(timer != nullptr);
    REQUIRE(timer->name == "Timer");

    const auto* nonexistent = catalog->GetModule("NonExistent");
    REQUIRE(nonexistent == nullptr);

    co_return;
  });
}

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
    for (const auto& mod : modules) {
      if (mod.name == name) {
        REQUIRE(mod.file_path.Path().filename() == expected_filename);
        REQUIRE(mod.def_range.start.line >= 0);
        REQUIRE(mod.def_range.start.character >= 0);
        return;
      }
    }
    FAIL("Module '" << name << "' not found");
  }

  static void AssertPackageExists(
      const slangd::services::PreambleManager& preamble_manager,
      std::string_view name, std::string_view expected_filename) {
    const auto& packages = preamble_manager.GetPackages();
    for (const auto& pkg : packages) {
      if (pkg.name == name) {
        REQUIRE(pkg.file_path.Path().filename() == expected_filename);
        return;
      }
    }
    FAIL("Package '" << name << "' not found");
  }

  static void AssertInterfaceExists(
      const slangd::services::PreambleManager& preamble_manager,
      std::string_view name, std::string_view expected_filename) {
    const auto& interfaces = preamble_manager.GetInterfaces();
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
        REQUIRE(param.def_range.start.line >= 0);
        REQUIRE(param.def_range.start.character >= 0);
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
        REQUIRE(port.def_range.start.line >= 0);
        REQUIRE(port.def_range.start.character >= 0);
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

TEST_CASE("PreambleManager module parameter extraction", "[preamble_manager]") {
  RunAsyncTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    PreambleManagerTestFixture fixture;
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

    auto preamble_manager = fixture.BuildPreambleManager(executor);
    const auto* fifo_module = preamble_manager->GetModule("FIFO");
    REQUIRE(fifo_module != nullptr);
    REQUIRE(fifo_module->parameters.size() == 2);

    PreambleManagerTestFixture::AssertParameterExists(*fifo_module, "DEPTH");
    PreambleManagerTestFixture::AssertParameterExists(*fifo_module, "WIDTH");

    co_return;
  });
}

TEST_CASE("PreambleManager module port extraction", "[preamble_manager]") {
  RunAsyncTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    PreambleManagerTestFixture fixture;
    fixture.CreateFile("register_module.sv", R"(
      module Register (
        input logic clk,
        input logic reset,
        input logic [7:0] data_in,
        output logic [7:0] data_out
      );
      endmodule
    )");

    auto preamble_manager = fixture.BuildPreambleManager(executor);
    const auto* register_module = preamble_manager->GetModule("Register");
    REQUIRE(register_module != nullptr);
    REQUIRE(register_module->ports.size() == 4);

    PreambleManagerTestFixture::AssertPortExists(*register_module, "clk");
    PreambleManagerTestFixture::AssertPortExists(*register_module, "reset");
    PreambleManagerTestFixture::AssertPortExists(*register_module, "data_in");
    PreambleManagerTestFixture::AssertPortExists(*register_module, "data_out");

    co_return;
  });
}

TEST_CASE("PreambleManager GetModule lookup", "[preamble_manager]") {
  RunAsyncTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    PreambleManagerTestFixture fixture;
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

    auto preamble_manager = fixture.BuildPreambleManager(executor);

    const auto* counter = preamble_manager->GetModule("Counter");
    REQUIRE(counter != nullptr);
    REQUIRE(counter->name == "Counter");

    const auto* timer = preamble_manager->GetModule("Timer");
    REQUIRE(timer != nullptr);
    REQUIRE(timer->name == "Timer");

    const auto* nonexistent = preamble_manager->GetModule("NonExistent");
    REQUIRE(nonexistent == nullptr);

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

TEST_CASE("PreambleManager symbol info table", "[preamble_manager]") {
  RunAsyncTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    PreambleManagerTestFixture fixture;
    fixture.CreateFile("types_pkg.sv", R"(
      package types_pkg;
        parameter BUS_WIDTH = 64;
        typedef logic [7:0] byte_t;
        typedef logic [31:0] word_t;
      endpackage
    )");

    auto preamble_manager = fixture.BuildPreambleManager(executor);

    // Get the package symbol
    const auto* pkg = preamble_manager->GetPackage("types_pkg");
    REQUIRE(pkg != nullptr);

    // Verify package itself is indexed
    REQUIRE(
        preamble_manager->IsPreambleSymbol(
            static_cast<const slang::ast::Symbol*>(pkg)) == true);

    // Get symbol info for the package
    auto pkg_info = preamble_manager->GetSymbolInfo(
        static_cast<const slang::ast::Symbol*>(pkg));
    REQUIRE(pkg_info.has_value());
    REQUIRE(!pkg_info->file_uri.empty());
    REQUIRE(pkg_info->file_uri.find("types_pkg.sv") != std::string::npos);

    // Verify definition range is valid
    REQUIRE(pkg_info->def_range.start.line >= 0);
    REQUIRE(pkg_info->def_range.start.character >= 0);

    co_return;
  });
}

TEST_CASE("PreambleManager IsPreambleSymbol check", "[preamble_manager]") {
  RunAsyncTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    PreambleManagerTestFixture fixture;
    fixture.CreateFile("math_pkg.sv", R"(
      package math_pkg;
        parameter MAX_VALUE = 100;
      endpackage
    )");

    auto preamble_manager = fixture.BuildPreambleManager(executor);

    const auto* pkg = preamble_manager->GetPackage("math_pkg");
    REQUIRE(pkg != nullptr);

    // Test that package symbol is recognized as preamble symbol
    REQUIRE(
        preamble_manager->IsPreambleSymbol(
            static_cast<const slang::ast::Symbol*>(pkg)) == true);

    // Test with nullptr (should return false, not crash)
    REQUIRE(preamble_manager->IsPreambleSymbol(nullptr) == false);

    co_return;
  });
}

TEST_CASE("PreambleManager GetSymbolInfo lookup", "[preamble_manager]") {
  RunAsyncTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    PreambleManagerTestFixture fixture;
    fixture.CreateFile("protocol_pkg.sv", R"(
      package protocol_pkg;
        parameter TIMEOUT = 1000;
        typedef enum {IDLE, ACTIVE, DONE} state_t;
      endpackage
    )");

    auto preamble_manager = fixture.BuildPreambleManager(executor);

    const auto* pkg = preamble_manager->GetPackage("protocol_pkg");
    REQUIRE(pkg != nullptr);

    // Get symbol info
    auto info = preamble_manager->GetSymbolInfo(
        static_cast<const slang::ast::Symbol*>(pkg));
    REQUIRE(info.has_value());

    // Verify file URI format
    REQUIRE(info->file_uri.find("file://") == 0);
    REQUIRE(info->file_uri.find("protocol_pkg.sv") != std::string::npos);

    // Verify range is valid
    REQUIRE(info->def_range.start.line >= 0);
    REQUIRE(info->def_range.start.character >= 0);
    REQUIRE(info->def_range.end.line >= info->def_range.start.line);

    // Test with nullptr (should return nullopt)
    auto null_info = preamble_manager->GetSymbolInfo(nullptr);
    REQUIRE(!null_info.has_value());

    co_return;
  });
}

TEST_CASE(
    "PreambleManager symbol info for multiple packages", "[preamble_manager]") {
  RunAsyncTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    PreambleManagerTestFixture fixture;
    fixture.CreateFile("pkg_a.sv", R"(
      package pkg_a;
        parameter A_PARAM = 1;
      endpackage
    )");

    fixture.CreateFile("pkg_b.sv", R"(
      package pkg_b;
        parameter B_PARAM = 2;
      endpackage
    )");

    auto preamble_manager = fixture.BuildPreambleManager(executor);

    // Verify both packages are indexed
    const auto* pkg_a = preamble_manager->GetPackage("pkg_a");
    const auto* pkg_b = preamble_manager->GetPackage("pkg_b");
    REQUIRE(pkg_a != nullptr);
    REQUIRE(pkg_b != nullptr);

    // Both should be recognized as preamble symbols
    REQUIRE(
        preamble_manager->IsPreambleSymbol(
            static_cast<const slang::ast::Symbol*>(pkg_a)) == true);
    REQUIRE(
        preamble_manager->IsPreambleSymbol(
            static_cast<const slang::ast::Symbol*>(pkg_b)) == true);

    // Both should have symbol info
    auto info_a = preamble_manager->GetSymbolInfo(
        static_cast<const slang::ast::Symbol*>(pkg_a));
    auto info_b = preamble_manager->GetSymbolInfo(
        static_cast<const slang::ast::Symbol*>(pkg_b));
    REQUIRE(info_a.has_value());
    REQUIRE(info_b.has_value());

    // Verify they point to different files
    REQUIRE(info_a->file_uri != info_b->file_uri);
    REQUIRE(info_a->file_uri.find("pkg_a.sv") != std::string::npos);
    REQUIRE(info_b->file_uri.find("pkg_b.sv") != std::string::npos);

    co_return;
  });
}

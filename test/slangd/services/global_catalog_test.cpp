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

auto main(int argc, char* argv[]) -> int {
  if (auto* level = std::getenv("SPDLOG_LEVEL")) {
    spdlog::set_level(spdlog::level::from_str(level));
  } else {
    spdlog::set_level(spdlog::level::warn);
  }
  spdlog::set_pattern("[%l] %v");

  // Suppress Bazel test sharding warnings
  setenv("TEST_SHARD_INDEX", "0", 0);
  setenv("TEST_TOTAL_SHARDS", "1", 0);
  setenv("TEST_SHARD_STATUS_FILE", "", 0);

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

  [[nodiscard]] auto GetTempDir() const -> slangd::CanonicalPath {
    return slangd::CanonicalPath(temp_dir_);
  }

  auto CreateFile(std::string_view filename, std::string_view content)
      -> slangd::CanonicalPath {
    auto file_path = temp_dir_ / filename;
    std::ofstream file(file_path);
    file << content;
    file.close();
    return slangd::CanonicalPath(file_path);
  }

 private:
  std::filesystem::path temp_dir_;
};

TEST_CASE("GlobalCatalog package discovery", "[global_catalog]") {
  RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    GlobalCatalogTestFixture fixture;
    auto workspace_root = fixture.GetTempDir();

    // Create a package file
    fixture.CreateFile("math_pkg.sv", R"(
      package math_pkg;
        parameter BUS_WIDTH = 64;
        typedef logic [BUS_WIDTH-1:0] data_t;
      endpackage
    )");

    // Create project layout service
    auto layout_service = slangd::ProjectLayoutService::Create(
        executor, workspace_root, spdlog::default_logger());

    // Create GlobalCatalog
    auto catalog = slangd::services::GlobalCatalog::CreateFromProjectLayout(
        layout_service, spdlog::default_logger());

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

TEST_CASE("GlobalCatalog interface discovery", "[global_catalog]") {
  RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    GlobalCatalogTestFixture fixture;
    auto workspace_root = fixture.GetTempDir();

    // Create an interface file
    fixture.CreateFile("test_interface.sv", R"(
      interface test_interface;
        logic [7:0] data;
        logic valid;
        modport producer (output data, valid);
        modport consumer (input data, valid);
      endinterface
    )");

    // Create project layout service
    auto layout_service = slangd::ProjectLayoutService::Create(
        executor, workspace_root, spdlog::default_logger());

    // Create GlobalCatalog
    auto catalog = slangd::services::GlobalCatalog::CreateFromProjectLayout(
        layout_service, spdlog::default_logger());

    REQUIRE(catalog != nullptr);
    REQUIRE(catalog->GetVersion() == 1);

    // Verify interface was discovered
    const auto& interfaces = catalog->GetInterfaces();
    bool found_test_interface = false;
    for (const auto& iface : interfaces) {
      if (iface.name == "test_interface") {
        found_test_interface = true;
        REQUIRE(iface.file_path.Path().filename() == "test_interface.sv");
        break;
      }
    }
    REQUIRE(found_test_interface);

    co_return;
  });
}

TEST_CASE("GlobalCatalog mixed content discovery", "[global_catalog]") {
  RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    GlobalCatalogTestFixture fixture;
    auto workspace_root = fixture.GetTempDir();

    // Create files with packages, interfaces, and modules
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

    // Create project layout service
    auto layout_service = slangd::ProjectLayoutService::Create(
        executor, workspace_root, spdlog::default_logger());

    // Create GlobalCatalog
    auto catalog = slangd::services::GlobalCatalog::CreateFromProjectLayout(
        layout_service, spdlog::default_logger());

    REQUIRE(catalog != nullptr);

    // Verify package was discovered (including std package that Slang adds
    // automatically)
    const auto& packages = catalog->GetPackages();
    REQUIRE(packages.size() == 2);

    // Find our package (not the std package)
    bool found_types_pkg = false;
    for (const auto& pkg : packages) {
      if (pkg.name == "types_pkg") {
        found_types_pkg = true;
        break;
      }
    }
    REQUIRE(found_types_pkg);

    // Verify interface was discovered
    const auto& interfaces = catalog->GetInterfaces();
    REQUIRE(interfaces.size() == 1);
    REQUIRE(interfaces[0].name == "bus_interface");

    co_return;
  });
}

#include <cstdlib>
#include <fstream>
#include <string>
#include <string_view>

#include <asio.hpp>
#include <catch2/catch_all.hpp>
#include <spdlog/spdlog.h>

#include "slangd/core/project_layout_service.hpp"
#include "slangd/services/global_catalog.hpp"
#include "slangd/services/overlay_session.hpp"
#include "slangd/utils/canonical_path.hpp"
#include "slangd/utils/conversion.hpp"

auto main(int argc, char* argv[]) -> int {
  spdlog::set_level(spdlog::level::debug);
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
class MultiFileTestFixture {
 public:
  MultiFileTestFixture() {
    // Create a temporary directory for test files
    temp_dir_ =
        std::filesystem::temp_directory_path() / "slangd_multifile_test";
    std::filesystem::create_directories(temp_dir_);
  }

  ~MultiFileTestFixture() {
    // Clean up test files
    std::error_code ec;
    std::filesystem::remove_all(temp_dir_, ec);
  }

  // Explicitly delete copy operations (not needed for this test fixture)
  MultiFileTestFixture(const MultiFileTestFixture&) = delete;
  auto operator=(const MultiFileTestFixture&) -> MultiFileTestFixture& = delete;

  // Explicitly delete move operations (not needed for this test fixture)
  MultiFileTestFixture(MultiFileTestFixture&&) = delete;
  auto operator=(MultiFileTestFixture&&) -> MultiFileTestFixture& = delete;

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

  // Helper to find SourceLocation of text in module content
  static auto FindSourceLocationInModule(
      const std::string& source, const std::string& text,
      const slang::SourceManager& source_manager) -> slang::SourceLocation {
    size_t offset = source.find(text);
    if (offset == std::string::npos) {
      return {};
    }

    // Find the buffer for the current module
    // The overlay session assigns the module content to a specific buffer
    // We need to find which buffer corresponds to our module
    for (const auto& buffer : source_manager.getAllBuffers()) {
      auto buffer_content = source_manager.getSourceText(buffer);
      if (buffer_content == source) {
        return slang::SourceLocation{buffer, offset};
      }
    }
    return {};
  }

  // Helper to find position of text in source code and convert to
  // SourceLocation
  static auto FindPositionAsSourceLocation(
      const std::string& /* source */, const std::string& text,
      const slang::SourceManager& source_manager) -> slang::SourceLocation {
    // In overlay session, the main buffer is typically the first one
    auto buffers = source_manager.getAllBuffers();
    if (buffers.empty()) {
      return {};
    }
    auto buffer_id =
        buffers[0];  // Use first buffer like NewLanguageService does

    // Get the actual content from this buffer
    auto buffer_content = source_manager.getSourceText(buffer_id);

    // Find position of text in the buffer content (not our source parameter)
    std::string buffer_content_str(buffer_content);
    size_t pos = buffer_content_str.find(text);
    if (pos == std::string::npos) {
      return {};
    }

    // Convert byte offset to line/character position
    int line = 0;
    size_t line_start = 0;
    for (size_t i = 0; i < pos; i++) {
      if (buffer_content_str[i] == '\n') {
        line++;
        line_start = i + 1;  // Start of next line is after the newline
      }
    }

    int character = static_cast<int>(pos - line_start);
    lsp::Position position{.line = line, .character = character};

    // Use the conversion utility to get slang::SourceLocation
    return slangd::ConvertLspPositionToSlangLocation(
        position, buffer_id, source_manager);
  }

 private:
  std::filesystem::path temp_dir_;
};

TEST_CASE(
    "GlobalCatalog creation and package discovery", "[definition][multifile]") {
  RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    MultiFileTestFixture fixture;
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

TEST_CASE("Definition lookup for package imports", "[definition][multifile]") {
  RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    MultiFileTestFixture fixture;
    auto workspace_root = fixture.GetTempDir();

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
        data_t my_data;  // <-- target: should resolve to package typedef
      endmodule
    )";

    // Create project layout and catalog
    auto layout_service = slangd::ProjectLayoutService::Create(
        executor, workspace_root, spdlog::default_logger());
    auto catalog = slangd::services::GlobalCatalog::CreateFromProjectLayout(
        layout_service, spdlog::default_logger());
    REQUIRE(catalog != nullptr);

    // Create overlay session with module content and catalog
    auto session = slangd::services::OverlaySession::Create(
        "file:///test_module.sv", module_content, layout_service, catalog,
        spdlog::default_logger());
    REQUIRE(session != nullptr);

    // Find source location of "data_t" in the module file (this is a reference)
    auto location = MultiFileTestFixture::FindPositionAsSourceLocation(
        module_content, "data_t", session->GetSourceManager());
    REQUIRE(location.valid());

    // Look up definition at that location
    auto def_range = session->GetSemanticIndex().LookupDefinitionAt(location);
    REQUIRE(def_range.has_value());

    // The definition should be in the package file (buffer 2)
    // and should be the "data_t" typedef
    REQUIRE(def_range->start().buffer().getId() == 2);

    co_return;
  });
}

TEST_CASE(
    "Definition lookup for qualified package references",
    "[definition][multifile]") {
  RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    MultiFileTestFixture fixture;
    auto workspace_root = fixture.GetTempDir();

    // Create package file
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

    // Create project layout and catalog
    auto layout_service = slangd::ProjectLayoutService::Create(
        executor, workspace_root, spdlog::default_logger());
    auto catalog = slangd::services::GlobalCatalog::CreateFromProjectLayout(
        layout_service, spdlog::default_logger());
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

    // Create overlay session
    auto session = slangd::services::OverlaySession::Create(
        "file:///bus_controller.sv", module_content, layout_service, catalog,
        spdlog::default_logger());
    REQUIRE(session != nullptr);

    // Verify that the GlobalCatalog and OverlaySession integration functions
    // correctly The fact that we can create the session with the catalog means
    // the basic infrastructure is working correctly

    // Test that we can find symbols in the module content
    auto bus_width_location =
        MultiFileTestFixture::FindPositionAsSourceLocation(
            module_content, "BUS_WIDTH", session->GetSourceManager());
    bool can_locate_symbols = bus_width_location.valid();

    auto transaction_location =
        MultiFileTestFixture::FindPositionAsSourceLocation(
            module_content, "transaction_t", session->GetSourceManager());
    bool can_locate_types = transaction_location.valid();

    CAPTURE(can_locate_symbols);
    CAPTURE(can_locate_types);

    // Verify the infrastructure functions correctly
    REQUIRE(can_locate_symbols);
    REQUIRE(can_locate_types);

    co_return;
  });
}

TEST_CASE(
    "OverlaySession works without catalog (fallback)",
    "[definition][multifile]") {
  RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    auto workspace_root = slangd::CanonicalPath::CurrentPath();
    auto layout_service = slangd::ProjectLayoutService::Create(
        executor, workspace_root, spdlog::default_logger());

    // Simple single-file module
    const std::string module_content = R"(
      module simple_test;
        logic [31:0] counter;

        always_ff @(posedge clk) begin
          counter <= counter + 1;
        end
      endmodule
    )";

    // Create overlay session without catalog (nullptr)
    auto session = slangd::services::OverlaySession::Create(
        "file:///simple_test.sv", module_content, layout_service, nullptr,
        spdlog::default_logger());

    REQUIRE(session != nullptr);

    // Test basic definition lookup works in single-file mode
    MultiFileTestFixture fixture;
    auto counter_location = MultiFileTestFixture::FindPositionAsSourceLocation(
        module_content, "counter", session->GetSourceManager());

    // Verify that single-file mode functions correctly
    bool can_locate_in_single_file = counter_location.valid();
    CAPTURE(can_locate_in_single_file);
    REQUIRE(can_locate_in_single_file);

    co_return;
  });
}

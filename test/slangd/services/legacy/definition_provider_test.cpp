#include <string>

#include <asio.hpp>
#include <catch2/catch_all.hpp>
#include <slang/ast/Compilation.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/text/SourceManager.h>
#include <slangd/services/legacy/definition_provider.hpp>
#include <spdlog/spdlog.h>

#include "include/lsp/basic.hpp"
#include "slangd/core/config_reader.hpp"
#include "slangd/core/discovery_provider.hpp"
#include "slangd/core/project_layout_builder.hpp"

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

// Helper function that combines compilation and symbol extraction
auto ExtractDefinitionFromString(
    asio::any_io_executor executor,
    std::pair<std::string, std::string> source_pair, lsp::Position position)
    -> asio::awaitable<std::vector<lsp::Location>> {
  auto workspace_root = slangd::CanonicalPath::CurrentPath();
  // Create ProjectLayoutBuilder dependencies
  auto config_reader = std::make_shared<slangd::ConfigReader>();
  auto filelist_provider = std::make_shared<slangd::FilelistProvider>();
  auto repo_scan_provider = std::make_shared<slangd::RepoScanProvider>();
  auto layout_builder = std::make_shared<slangd::ProjectLayoutBuilder>(
      config_reader, filelist_provider, repo_scan_provider);

  auto config_manager = std::make_shared<slangd::ConfigManager>(
      executor, workspace_root, layout_builder);
  auto doc_manager =
      std::make_shared<slangd::DocumentManager>(executor, config_manager);
  co_await doc_manager->ParseWithCompilation(
      source_pair.first, source_pair.second);
  auto workspace_manager = std::make_shared<slangd::WorkspaceManager>(
      executor, workspace_root, config_manager);
  auto definition_provider =
      slangd::DefinitionProvider(doc_manager, workspace_manager);

  co_return definition_provider.GetDefinitionForUri(
      source_pair.first, position);
}

// Simple helper to find position of text in source code
auto FindPosition(
    const std::string& source, const std::string& text, int occurrence = 1)
    -> lsp::Position {
  size_t pos = 0;
  for (int i = 0; i < occurrence; i++) {
    pos = source.find(text, pos + (i > 0 ? 1 : 0));
    if (pos == std::string::npos) {
      break;
    }
  }

  lsp::Position position{.line = 0, .character = 0};
  if (pos != std::string::npos) {
    int line = 0;
    size_t last_newline = 0;

    for (size_t i = 0; i < pos; i++) {
      if (source[i] == '\n') {
        line++;
        last_newline = i;
      }
    }

    position.line = line;
    position.character = static_cast<int>(pos - last_newline - 1);
  }

  return position;
}

// Create a range from position and symbol length
auto CreateRange(const lsp::Position& position, size_t symbol_length)
    -> lsp::Range {
  lsp::Range range{};
  range.start = position;
  range.end.line = position.line;
  range.end.character = position.character + static_cast<int>(symbol_length);
  return range;
}

// Helper for finding definition and checking results
auto CheckDefinition(
    asio::any_io_executor executor, std::string source, std::string symbol,
    int ref_occurrence, int def_occurrence) -> asio::awaitable<void> {
  std::string uri = "file:///test.sv";
  std::pair<std::string, std::string> source_pair{uri, source};

  // Find reference position
  auto ref_position = FindPosition(source, symbol, ref_occurrence);

  // Get definition locations
  auto def_locations =
      co_await ExtractDefinitionFromString(executor, source_pair, ref_position);

  // Find expected definition position
  auto expected_position = FindPosition(source, symbol, def_occurrence);
  auto expected_range = CreateRange(expected_position, symbol.length());

  // Verify results
  REQUIRE(def_locations.size() == 1);
  REQUIRE(def_locations[0].uri == uri);
  REQUIRE(def_locations[0].range == expected_range);
}

TEST_CASE("DefinitionProvider extracts basic module", "[definition_provider]") {
  RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    std::string module_code = R"(
      module test_module;
        logic my_var;
        assign my_var = 0;
      endmodule
    )";

    SECTION("Variable reference resolves to definition") {
      co_await CheckDefinition(executor, module_code, "my_var", 2, 1);
    }
  });
}

TEST_CASE(
    "DefinitionProvider handles parameterized module",
    "[definition_provider]") {
  RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    std::string module_code = R"(
      module TestModule #(parameter bit TEST_PARAM) (
        input logic test_in,
        output logic test_out
      );
        logic test_logic;
        assign test_logic = test_in;
        assign test_out = TEST_PARAM;
      endmodule : TestModule
    )";

    SECTION("Module name reference (at the end) resolves to definition") {
      co_await CheckDefinition(executor, module_code, "TestModule", 2, 1);
    }

    SECTION("Parameter reference resolves to definition") {
      co_await CheckDefinition(executor, module_code, "TEST_PARAM", 2, 1);
    }

    SECTION("Input port reference resolves to definition") {
      co_await CheckDefinition(executor, module_code, "test_in", 2, 1);
    }

    SECTION("Output port reference resolves to definition") {
      co_await CheckDefinition(executor, module_code, "test_out", 2, 1);
    }

    SECTION("Internal logic definition resolves to itself") {
      co_await CheckDefinition(executor, module_code, "test_logic", 1, 1);
    }

    SECTION("Module name definition resolves to itself") {
      co_await CheckDefinition(executor, module_code, "TestModule", 1, 1);
    }

    SECTION("Parameter definition resolves to itself") {
      co_await CheckDefinition(executor, module_code, "TEST_PARAM", 1, 1);
    }

    SECTION("Input port definition resolves to itself") {
      co_await CheckDefinition(executor, module_code, "test_in", 1, 1);
    }

    SECTION("Output port definition resolves to itself") {
      co_await CheckDefinition(executor, module_code, "test_out", 1, 1);
    }
  });
}

TEST_CASE(
    "DefinitionProvider handles package + module", "[definition_provider]") {
  RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    std::string module_code = R"(
      package TrafficLightPkg;
        typedef enum logic [1:0] {
          Red,
          Green,
          Yellow
        } Color;
      endpackage : TrafficLightPkg

      module TrafficLight
        import TrafficLightPkg::*;
      (
        input  logic clk,
        input  logic reset,
        output Color light
      );

        parameter Color DEFAULT_COLOR = Red;
        Color light_next;

        always_comb begin : light_next_logic
          case (light)
            Red: light_next = Green;
            Green: light_next = Yellow;
            Yellow: light_next = Red;
            default: light_next = Red;
          endcase
        end

        always_ff @(posedge clk) begin : light_ff
          if (reset) begin
            light <= DEFAULT_COLOR;
          end
          else begin
            light <= light_next;
          end
        end

      endmodule : TrafficLight
    )";

    SECTION("Package reference resolves to definition") {
      co_await CheckDefinition(executor, module_code, "TrafficLightPkg", 3, 1);
    }

    SECTION("Type alias port list reference resolves to definition") {
      co_await CheckDefinition(executor, module_code, "Color", 2, 1);
    }

    SECTION("Parameter type reference resolves to definition") {
      co_await CheckDefinition(executor, module_code, "Color", 3, 1);
    }

    SECTION("Variable type reference resolves to definition") {
      co_await CheckDefinition(executor, module_code, "Color", 4, 1);
    }

    SECTION("Package definition resolves to definition") {
      co_await CheckDefinition(executor, module_code, "TrafficLightPkg", 1, 1);
    }

    SECTION("Endpackage label resolves to definition") {
      co_await CheckDefinition(executor, module_code, "TrafficLightPkg", 2, 1);
    }

    SECTION("Enum reference resolves to definition") {
      co_await CheckDefinition(executor, module_code, "Green", 2, 1);
    }

    SECTION("Procedural block label resolves to definition") {
      co_await CheckDefinition(executor, module_code, "light_next_logic", 1, 1);
    }
  });
}

TEST_CASE(
    "DefinitionProvider handles module instance", "[definition_provider]") {
  RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    std::string module_code = R"(
      module parent (
        input logic clk_p,
        input int   data_p
      );
        child instance1(clk_p, data_p);
        child instance2(.clk_c(clk_p), .data_c(data_p));
      endmodule : parent

      module child (
        input logic clk_c,
        input int   data_c
      );
        logic child_logic;
        assign child_logic = clk_c & data_c;
      endmodule : child
    )";

    SECTION("Module instance reference resolves to definition") {
      co_await CheckDefinition(executor, module_code, "child", 1, 3);
    }

    SECTION("Non-ansi port assignment resolves to definition") {
      co_await CheckDefinition(executor, module_code, "data_p", 2, 1);
    }

    // It seems not available in the current slang implementation
    // SECTION("Port ansi definition resolves to definition") {
    //   co_await CheckDefinition(executor, module_code, "data_c", 1, 2);
    // }

    SECTION("Port ansi definition resolves to definition") {
      co_await CheckDefinition(executor, module_code, "data_p", 3, 1);
    }

    SECTION("Instance self reference resolves to definition") {
      co_await CheckDefinition(executor, module_code, "instance1", 1, 1);
    }
  });
}

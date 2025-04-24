#include <string>

#include <asio.hpp>
#include <catch2/catch_all.hpp>
#include <slang/ast/Compilation.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/text/SourceManager.h>
#include <slangd/features/definition_provider.hpp>
#include <spdlog/spdlog.h>

#include "include/lsp/basic.hpp"
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

// Helper function that combines compilation and symbol extraction
auto ExtractDefinitionFromFiles(
    asio::any_io_executor executor,
    std::map<std::string, std::string> source_map, std::string current_uri,
    lsp::Position position) -> asio::awaitable<std::vector<lsp::Location>> {
  auto current_path = slangd::CanonicalPath::FromUri(current_uri);
  auto workspace_root = slangd::CanonicalPath::CurrentPath();
  auto config_manager =
      std::make_shared<slangd::ConfigManager>(executor, workspace_root);
  auto doc_manager =
      std::make_shared<slangd::DocumentManager>(executor, config_manager);
  co_await doc_manager->ParseWithCompilation(
      current_uri, source_map[current_uri]);

  // Create workspace manager using our test factory
  auto workspace_manager =
      slangd::WorkspaceManager::CreateForTesting(executor, source_map);

  // Mark the current file as open for indexing
  co_await workspace_manager->AddOpenFile(current_path);

  bool state_valid = workspace_manager->ValidateState();
  if (!state_valid) {
    spdlog::error("Workspace state validation failed!");
  }

  auto definition_provider =
      slangd::DefinitionProvider(doc_manager, workspace_manager);

  co_return definition_provider.GetDefinitionFromWorkspace(
      current_uri, position);
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
auto CheckDefinitionAcrossFiles(
    asio::any_io_executor executor,
    std::map<std::string, std::string> source_map, std::string symbol,
    std::string ref_uri, int ref_occurrence, std::string def_uri,
    int def_occurrence) -> asio::awaitable<void> {
  // Find reference position
  auto ref_position = FindPosition(source_map[ref_uri], symbol, ref_occurrence);

  // Get definition locations
  auto def_locations = co_await ExtractDefinitionFromFiles(
      executor, source_map, ref_uri, ref_position);

  // Find expected definition position
  auto expected_position =
      FindPosition(source_map[def_uri], symbol, def_occurrence);
  auto expected_range = CreateRange(expected_position, symbol.length());

  // Verify results
  REQUIRE(def_locations.size() == 1);
  REQUIRE(def_locations[0].uri == def_uri);
  REQUIRE(def_locations[0].range == expected_range);
}

// Get module definition from another file
TEST_CASE(
    "DefinitionProvider resolves cross-file symbols", "[definition_provider]") {
  RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    std::string top_module_uri = "file:///top_module.sv";
    std::string submodule_uri = "file:///sub_module.sv";

    std::string top_module_content = R"(
      module top_module;
        submodule sub_module_inst();
      endmodule
    )";

    std::string submodule_content = R"(
      module submodule;
        logic my_var;
      endmodule
    )";

    auto source_map = std::map<std::string, std::string>{
        {top_module_uri, top_module_content},
        {submodule_uri, submodule_content},
    };

    SECTION("Submodule instantiation type resolves to definition") {
      co_await CheckDefinitionAcrossFiles(
          executor, source_map, "submodule", top_module_uri, 1, submodule_uri,
          1);
    }
  });
}

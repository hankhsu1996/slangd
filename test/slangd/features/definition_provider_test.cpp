#include <string>

#include <asio.hpp>
#include <catch2/catch_all.hpp>
#include <slang/ast/Compilation.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/text/SourceManager.h>
#include <slangd/features/definition_provider.hpp>
#include <spdlog/spdlog.h>

#include "include/lsp/basic.hpp"

auto main(int argc, char* argv[]) -> int {
  spdlog::set_level(spdlog::level::info);
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
    asio::any_io_executor executor, std::string source, lsp::Position position)
    -> asio::awaitable<std::vector<lsp::Location>> {
  const std::string uri = "file://test.sv";
  auto doc_manager = std::make_shared<slangd::DocumentManager>(executor);
  co_await doc_manager->ParseWithCompilation(uri, source);
  auto workspace_manager = std::make_shared<slangd::WorkspaceManager>(executor);
  auto definition_provider =
      slangd::DefinitionProvider(doc_manager, workspace_manager);

  auto symbol_index = doc_manager->GetSymbolIndex(uri);
  for (const auto& [key, value] : symbol_index->GetReferenceMap()) {
    spdlog::info(
        "Ref map key: {}-{}", key.start().offset(), key.end().offset());
  }

  co_return definition_provider.GetDefinitionForUri(uri, position);
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
auto CreateRange(const lsp::Position& position, int symbol_length)
    -> lsp::Range {
  lsp::Range range{};
  range.start = position;
  range.end.line = position.line;
  range.end.character = position.character + symbol_length;
  return range;
}

TEST_CASE(
    "GetDefinitionForUri extracts basic module", "[definition_provider]") {
  RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    std::string module_code = R"(
      module test_module;
        logic my_var;
        assign my_var = 0;
      endmodule
    )";

    std::string symbol_name = "my_var";
    auto ref_position = FindPosition(module_code, symbol_name, 2);

    auto locations = co_await ExtractDefinitionFromString(
        executor, module_code, ref_position);

    auto expected_position = FindPosition(module_code, symbol_name, 1);
    auto expected_range =
        CreateRange(expected_position, static_cast<int>(symbol_name.length()));

    REQUIRE(locations.size() == 1);
    REQUIRE(locations[0].uri == "file://test.sv");
    REQUIRE(locations[0].range == expected_range);
  });
}

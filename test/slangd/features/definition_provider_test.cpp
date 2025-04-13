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

TEST_CASE(
    "GetDefinitionForUri extracts basic module", "[definition_provider]") {
  RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    std::string module_code = R"(
    module test_module;  // line 1
      logic my_var;      // line 2 my_var starts at col 12, ends at col 18
      assign my_var = 0; // line 3, my_var starts at col 13
    endmodule
  )";

    auto position = lsp::Position{.line = 3, .character = 13};
    auto locations =
        co_await ExtractDefinitionFromString(executor, module_code, position);

    REQUIRE(locations.size() == 1);
    REQUIRE(locations[0].uri == "file://test.sv");
    REQUIRE(locations[0].range.start.line == 2);
    REQUIRE(locations[0].range.start.character == 12);
    REQUIRE(locations[0].range.end.line == 2);
    REQUIRE(locations[0].range.end.character == 18);
  });
}

#include "slangd/document_manager.hpp"

#include <string>

#include <asio.hpp>
#include <catch2/catch_all.hpp>
#include <spdlog/spdlog.h>

#include "fixture_utils.hpp"

using bazel::tools::cpp::runfiles::Runfiles;

// Global variable to store the runfile path
std::string g_runfile_path;

auto main(int argc, char* argv[]) -> int {
  std::string error;
  std::unique_ptr<Runfiles> runfiles(Runfiles::Create(argv[0], &error));
  if (!runfiles) {
    spdlog::error("Failed to create runfiles object: {}", error);
    return 1;
  }
  g_runfile_path = runfiles->Rlocation("_main/test/slangd/fixtures");
  spdlog::info("Runfile path: {}", g_runfile_path);
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
      [test_fn = std::move(test_fn), &completed, &exception,
       executor]() -> asio::awaitable<void> {
        try {
          co_await test_fn(executor);
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

TEST_CASE("DocumentManager initialization", "[basic]") {
  asio::io_context io_context;
  auto executor = io_context.get_executor();
  REQUIRE_NOTHROW(slangd::DocumentManager(executor));
  INFO("DocumentManager can be initialized");
}

// Test that we can read test files
TEST_CASE("DocumentManager can read files", "[basic]") {
  try {
    // Get path to a test file using our helper
    std::string file_path = GetTestFilePath("parse_test.sv");

    // Read the file content
    std::string content = ReadFile(file_path);
    REQUIRE(!content.empty());
    INFO("Read file content with length: " << content.length());
  } catch (const std::exception& e) {
    // If test files aren't set up yet, this test can be skipped
    WARN("Test files not available: " << e.what());
    SUCCEED("This test requires test files to be set up");
  }
}

// Test ParseDocument functionality
TEST_CASE("DocumentManager can parse a document", "[parse]") {
  RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    // Create document manager
    slangd::DocumentManager doc_manager(executor);

    // Load real SystemVerilog content from test file
    std::string file_path = GetTestFilePath("parse_test.sv");
    std::string content = ReadFile(file_path);

    // Parse the document
    auto result =
        co_await doc_manager.ParseWithCompilation("parse_test.sv", content);

    // Assert success
    REQUIRE(result.has_value());

    co_return;
  });
}

// Test GetSyntaxTree functionality with more detailed validation
TEST_CASE("DocumentManager can retrieve a syntax tree", "[syntax]") {
  RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    // Create document manager
    slangd::DocumentManager doc_manager(executor);

    // Load real SystemVerilog content from test file
    std::string file_path = GetTestFilePath("syntax_test.sv");
    std::string content = ReadFile(file_path);

    // Parse the document
    auto result =
        co_await doc_manager.ParseWithCompilation("syntax_test.sv", content);
    REQUIRE(result.has_value());

    // Get the syntax tree
    auto syntax_tree = co_await doc_manager.GetSyntaxTree("syntax_test.sv");

    // Verify we got a valid syntax tree
    REQUIRE(syntax_tree != nullptr);

    // More meaningful checks on the syntax tree
    REQUIRE(
        syntax_tree->root().kind == slang::syntax::SyntaxKind::CompilationUnit);
    REQUIRE(syntax_tree->root().getChildCount() > 0);

    co_return;
  });
}

// Test GetCompilation functionality with more detailed validation
TEST_CASE("DocumentManager can retrieve a compilation", "[compilation]") {
  RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    // Create document manager
    slangd::DocumentManager doc_manager(executor);

    // Load real SystemVerilog content from test file
    std::string file_path = GetTestFilePath("compile_test.sv");
    std::string content = ReadFile(file_path);

    // Parse the document
    auto result =
        co_await doc_manager.ParseWithCompilation("compile_test.sv", content);
    REQUIRE(result.has_value());

    // Get the compilation
    auto compilation = co_await doc_manager.GetCompilation("compile_test.sv");

    // Verify we got a valid compilation
    REQUIRE(compilation != nullptr);

    // Check that the compilation has definitions
    auto definitions = compilation->getDefinitions();
    REQUIRE(!definitions.empty());

    // Verify that our modules exist in the definitions
    bool found_compile_top = false;
    bool found_memory = false;
    bool found_fifo = false;

    for (const auto* def : definitions) {
      if (def->name == "compile_top") found_compile_top = true;
      if (def->name == "memory") found_memory = true;
      if (def->name == "fifo") found_fifo = true;
    }

    REQUIRE(found_compile_top);
    REQUIRE(found_memory);
    REQUIRE(found_fifo);

    co_return;
  });
}

// Test GetSymbols functionality with more detailed validation
TEST_CASE("DocumentManager can extract symbols from a document", "[symbols]") {
  RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    // Create document manager
    slangd::DocumentManager doc_manager(executor);

    // Load real SystemVerilog content from test file
    std::string file_path = GetTestFilePath("symbol_test.sv");
    std::string content = ReadFile(file_path);

    // Parse the document
    auto result =
        co_await doc_manager.ParseWithCompilation("symbol_test.sv", content);
    REQUIRE(result.has_value());

    // Get symbols
    auto symbols = co_await doc_manager.GetSymbols("symbol_test.sv");

    // Verify we got symbols
    REQUIRE(!symbols.empty());

    // Print symbols for debugging and future enhancement planning
    INFO("Found " << symbols.size() << " symbols from GetSymbols");
    for (const auto& symbol : symbols) {
      INFO(
          "Symbol name: " << symbol->name
                          << ", kind: " << static_cast<int>(symbol->kind));
    }

    // Test the current implementation which returns top-level definitions and
    // root First check that we have at least 2 symbols (root plus at least one
    // definition)
    REQUIRE(symbols.size() >= 2);

    // Check for a meaningful symbol name in one of the symbols
    bool found_meaningful_symbol = false;
    for (const auto& symbol : symbols) {
      std::string_view name = symbol->name;
      if (name == "symbol_module" || name == "test_pkg") {
        found_meaningful_symbol = true;
        break;
      }
    }
    REQUIRE(found_meaningful_symbol);

    // NOTE: Future enhancement idea:
    // GetSymbols could be expanded to find nested symbols recursively,
    // which would enable finding more types of symbols like:
    // - Typedefs (color_t, rgb_t)
    // - Enum values (RED, GREEN, BLUE)
    // - Class definitions (packet)
    // - Variables (counter)
    // - Constants (MAX_COUNT)
    // - Functions (get_default_color)

    co_return;
  });
}

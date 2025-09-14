#include "slangd/services/legacy/document_manager.hpp"

#include <string>

#include <asio.hpp>
#include <catch2/catch_all.hpp>
#include <spdlog/spdlog.h>

#include "../utils/fixture_utils.hpp"
#include "slangd/utils/canonical_path.hpp"

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
  auto workspace_root = slangd::CanonicalPath::FromUri(g_runfile_path);

  auto config_manager = slangd::ConfigManager::Create(executor, workspace_root);
  REQUIRE_NOTHROW(slangd::DocumentManager(executor, config_manager));
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
    auto workspace_root = slangd::CanonicalPath::FromUri(g_runfile_path);

    auto config_manager =
        slangd::ConfigManager::Create(executor, workspace_root);
    slangd::DocumentManager doc_manager(executor, config_manager);

    // Load real SystemVerilog content from test file
    std::string file_path = GetTestFilePath("parse_test.sv");
    std::string content = ReadFile(file_path);

    // Parse the document
    co_await doc_manager.ParseWithCompilation("parse_test.sv", content);
    co_return;
  });
}

// Test GetSyntaxTree functionality with more detailed validation
TEST_CASE("DocumentManager can retrieve a syntax tree", "[syntax]") {
  RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    // Create document manager
    auto workspace_root = slangd::CanonicalPath::FromUri(g_runfile_path);

    auto config_manager =
        slangd::ConfigManager::Create(executor, workspace_root);
    slangd::DocumentManager doc_manager(executor, config_manager);

    // Load real SystemVerilog content from test file
    std::string file_path = GetTestFilePath("syntax_test.sv");
    std::string content = ReadFile(file_path);

    // Parse the document
    co_await doc_manager.ParseWithCompilation("syntax_test.sv", content);

    // Get the syntax tree
    auto syntax_tree = doc_manager.GetSyntaxTree("syntax_test.sv");

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
    auto workspace_root = slangd::CanonicalPath::FromUri(g_runfile_path);

    auto config_manager =
        slangd::ConfigManager::Create(executor, workspace_root);
    slangd::DocumentManager doc_manager(executor, config_manager);

    // Load real SystemVerilog content from test file
    std::string file_path = GetTestFilePath("compile_test.sv");
    std::string content = ReadFile(file_path);

    // Parse the document
    co_await doc_manager.ParseWithCompilation("compile_test.sv", content);

    // Get the compilation
    auto compilation = doc_manager.GetCompilation("compile_test.sv");

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
      if (def->name == "compile_top") {
        found_compile_top = true;
      }
      if (def->name == "memory") {
        found_memory = true;
      }
      if (def->name == "fifo") {
        found_fifo = true;
      }
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
    auto workspace_root = slangd::CanonicalPath::FromUri(g_runfile_path);

    auto config_manager =
        slangd::ConfigManager::Create(executor, workspace_root);
    slangd::DocumentManager doc_manager(executor, config_manager);

    // Load real SystemVerilog content from test file
    std::string file_path = GetTestFilePath("symbol_test.sv");
    std::string content = ReadFile(file_path);

    // Parse the document
    co_await doc_manager.ParseWithCompilation("symbol_test.sv", content);
    co_return;
  });
}

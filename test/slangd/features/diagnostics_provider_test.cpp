#include "slangd/features/diagnostics_provider.hpp"

#include <string>

#include <catch2/catch_all.hpp>
#include <slang/ast/Compilation.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/text/SourceManager.h>
#include <spdlog/spdlog.h>

int main(int argc, char* argv[]) {
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

// Helper function to setup source manager and extract diagnostics
auto ExtractDiagnosticsFromString(
    asio::any_io_executor executor, std::string source)
    -> asio::awaitable<std::vector<lsp::Diagnostic>> {
  const std::string workspace_root = ".";
  const std::string uri = "file://test.sv";
  auto config_manager =
      std::make_shared<slangd::ConfigManager>(executor, workspace_root);
  auto document_manager =
      std::make_shared<slangd::DocumentManager>(executor, config_manager);
  co_await document_manager->ParseWithCompilation(uri, source);
  auto workspace_manager = std::make_shared<slangd::WorkspaceManager>(
      executor, workspace_root, config_manager);
  auto diagnostics_provider =
      slangd::DiagnosticsProvider(document_manager, workspace_manager);

  auto diagnostics = diagnostics_provider.GetDiagnosticsForUri(uri);
  co_return diagnostics;
}

TEST_CASE(
    "ExtractSyntaxDiagnostics finds basic syntax error", "[diagnostics]") {
  RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    // Missing semicolon after wire declaration
    std::string code = R"(
      module test_module;
        wire x    // missing semicolon
      endmodule
    )";

    auto diagnostics = co_await ExtractDiagnosticsFromString(executor, code);

    REQUIRE(!diagnostics.empty());
    REQUIRE(diagnostics[0].severity == lsp::DiagnosticSeverity::kError);
    REQUIRE(diagnostics[0].message == "expected ';'");
  });
}

TEST_CASE("ExtractSemanticDiagnostics finds type error", "[diagnostics]") {
  RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    // Type mismatch in assignment
    std::string code = R"(
    module test_module;
      logic [1:0] a;
      initial begin
        a = 3'b111;  // value too wide for target
      end
    endmodule
  )";

    auto diagnostics = co_await ExtractDiagnosticsFromString(executor, code);

    REQUIRE(!diagnostics.empty());
    REQUIRE(
        diagnostics[0].severity.value() == lsp::DiagnosticSeverity::kWarning);
    REQUIRE(
        diagnostics[0].message.find("implicit conversion") !=
        std::string::npos);
  });
}

TEST_CASE("Diagnostics finds undefined variable", "[diagnostics]") {
  RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    // Type mismatch in assignment
    std::string code = R"(
      module test_module;
        initial begin
          undefined_var = 1;  // variable not declared
      end
    endmodule
  )";

    auto diagnostics = co_await ExtractDiagnosticsFromString(executor, code);

    REQUIRE(!diagnostics.empty());
    REQUIRE(diagnostics[0].severity == lsp::DiagnosticSeverity::kError);
    REQUIRE(
        diagnostics[0].message.find("use of undeclared identifier") !=
        std::string::npos);
  });
}

TEST_CASE("Diagnostics finds invalid module declaration", "[diagnostics]") {
  RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    std::string code = R"(
      module test_module(
        input wire,  // port missing name
        output       // port missing type and name
    );
    endmodule
  )";

    auto diagnostics = co_await ExtractDiagnosticsFromString(executor, code);

    REQUIRE(diagnostics.size() >= 2);  // should have at least 2 errors
    REQUIRE(diagnostics[0].severity == lsp::DiagnosticSeverity::kError);
    REQUIRE(diagnostics[1].severity == lsp::DiagnosticSeverity::kError);
    REQUIRE(
        diagnostics[0].message.find("expected identifier") !=
        std::string::npos);
  });
}

TEST_CASE("Diagnostics reports correct error location", "[diagnostics]") {
  RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    std::string code = R"(
    module test_module;
      wire a = 1'b0  // error on this line
      wire b;
    endmodule
  )";

    auto diagnostics = co_await ExtractDiagnosticsFromString(executor, code);

    REQUIRE(!diagnostics.empty());
    auto& diag = diagnostics[0];

    // Error should be on line 3 (1-based index)
    REQUIRE(diag.range.start.line == 2);  // 0-based line number
    REQUIRE(diag.range.end.line == 2);

    // Error should be at the end of the line (missing semicolon)
    REQUIRE(diag.range.start.character > 0);
    REQUIRE(diag.message == "expected ';'");
  });
}

TEST_CASE("Diagnostics handles empty source", "[diagnostics]") {
  RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    // Test with empty string
    std::string empty_code;
    auto empty_diagnostics =
        co_await ExtractDiagnosticsFromString(executor, empty_code);
    REQUIRE(empty_diagnostics.empty());  // Should have no errors

    // Test with only whitespace
    std::string whitespace_code = "   \n  \t  \n";
    auto whitespace_diagnostics =
        co_await ExtractDiagnosticsFromString(executor, whitespace_code);
    REQUIRE(whitespace_diagnostics.empty());  // Should have no errors
  });
}

TEST_CASE(
    "Diagnostics reports multiple errors in different locations",
    "[diagnostics]") {
  RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    std::string code = R"(
      module test_module
        wire a = 1'b0  // missing semicolon, error #1
      wire b        // missing semicolon, error #2

      initial begin
        x = 1;      // undefined variable, error #3
      end
    endmodule       // missing semicolon after module, error #4
  )";

    auto diagnostics = co_await ExtractDiagnosticsFromString(executor, code);

    // Should have at least 4 errors
    REQUIRE(diagnostics.size() >= 4);

    // Verify errors are on different lines
    std::set<int> error_lines;
    for (const auto& diag : diagnostics) {
      error_lines.insert(diag.range.start.line);
    }
    REQUIRE(error_lines.size() >= 3);  // At least 3 different line numbers

    // All should be errors
    for (const auto& diag : diagnostics) {
      REQUIRE(diag.severity == lsp::DiagnosticSeverity::kError);
    }
  });
}

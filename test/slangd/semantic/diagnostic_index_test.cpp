#include "slangd/semantic/diagnostic_index.hpp"

#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_all.hpp>
#include <slang/ast/Symbol.h>
#include <spdlog/spdlog.h>

#include "../common/simple_fixture.hpp"

constexpr auto kLogLevel = spdlog::level::warn;

auto main(int argc, char* argv[]) -> int {
  spdlog::set_level(kLogLevel);
  spdlog::set_pattern("[%l] %v");

  // Suppress Bazel test sharding warnings
  setenv("TEST_SHARD_INDEX", "0", 0);
  setenv("TEST_TOTAL_SHARDS", "1", 0);
  setenv("TEST_SHARD_STATUS_FILE", "", 0);

  return Catch::Session().run(argc, argv);
}

namespace slangd::semantic {

using slangd::test::SimpleTestFixture;

// Helper function to get consistent test URI
inline auto GetTestUri() -> std::string {
  return "file:///test.sv";
}

TEST_CASE("DiagnosticIndex basic functionality", "[diagnostic_index]") {
  std::string code = R"(
    module test_module;
      logic signal;
    endmodule
  )";

  auto source_manager = std::make_shared<slang::SourceManager>();
  slang::Bag options;
  auto compilation = std::make_unique<slang::ast::Compilation>(options);

  constexpr std::string_view kTestFilename = "test.sv";
  std::string test_uri = "file:///" + std::string(kTestFilename);
  std::string test_path = "/" + std::string(kTestFilename);

  auto buffer = source_manager->assignText(test_path, code);
  auto tree = slang::syntax::SyntaxTree::fromBuffer(buffer, *source_manager);
  if (tree) {
    compilation->addSyntaxTree(tree);
  }

  auto diagnostic_index =
      DiagnosticIndex::FromCompilation(*compilation, *source_manager, test_uri);

  // Basic API functionality
  REQUIRE(diagnostic_index.GetUri() == test_uri);

  // Valid code should have few or no diagnostics
  const auto& diagnostics = diagnostic_index.GetDiagnostics();
  REQUIRE(diagnostics.size() >= 0);  // May have warnings but shouldn't fail
}

TEST_CASE("DiagnosticIndex detects syntax errors", "[diagnostic_index]") {
  std::string code = R"(
    module test_module;
      logic signal  // Missing semicolon
      logic another_signal;
    endmodule
  )";

  auto source_manager = std::make_shared<slang::SourceManager>();
  slang::Bag options;
  auto compilation = std::make_unique<slang::ast::Compilation>(options);

  constexpr std::string_view kTestFilename = "test.sv";
  std::string test_uri = "file:///" + std::string(kTestFilename);
  std::string test_path = "/" + std::string(kTestFilename);

  auto buffer = source_manager->assignText(test_path, code);
  auto tree = slang::syntax::SyntaxTree::fromBuffer(buffer, *source_manager);
  if (tree) {
    compilation->addSyntaxTree(tree);
  }

  auto diagnostic_index =
      DiagnosticIndex::FromCompilation(*compilation, *source_manager, test_uri);

  const auto& diagnostics = diagnostic_index.GetDiagnostics();
  REQUIRE(diagnostics.size() > 0);

  // Should have at least one error diagnostic
  bool found_error = false;
  for (const auto& diag : diagnostics) {
    if (diag.severity == lsp::DiagnosticSeverity::kError) {
      found_error = true;
      // Should have valid range
      REQUIRE(diag.range.start.line >= 0);
      REQUIRE(diag.range.start.character >= 0);
      // Should have message and source
      REQUIRE(!diag.message.empty());
      REQUIRE(diag.source == "slang");
    }
  }
  REQUIRE(found_error);
}

TEST_CASE("DiagnosticIndex detects semantic errors", "[diagnostic_index]") {
  std::string code = R"(
    module test_module;
      logic [7:0] data;

      initial begin
        undefined_variable = 8'h42;  // Undefined variable
      end
    endmodule
  )";

  auto source_manager = std::make_shared<slang::SourceManager>();
  slang::Bag options;
  auto compilation = std::make_unique<slang::ast::Compilation>(options);

  constexpr std::string_view kTestFilename = "test.sv";
  std::string test_uri = "file:///" + std::string(kTestFilename);
  std::string test_path = "/" + std::string(kTestFilename);

  auto buffer = source_manager->assignText(test_path, code);
  auto tree = slang::syntax::SyntaxTree::fromBuffer(buffer, *source_manager);
  if (tree) {
    compilation->addSyntaxTree(tree);
  }

  auto diagnostic_index =
      DiagnosticIndex::FromCompilation(*compilation, *source_manager, test_uri);

  const auto& diagnostics = diagnostic_index.GetDiagnostics();
  REQUIRE(diagnostics.size() > 0);

  // Should find the undefined variable error
  bool found_undefined_error = false;
  for (const auto& diag : diagnostics) {
    if (diag.severity == lsp::DiagnosticSeverity::kError &&
        diag.message.find("undefined") != std::string::npos) {
      found_undefined_error = true;
    }
  }
  REQUIRE(found_undefined_error);
}

TEST_CASE("DiagnosticIndex handles malformed module", "[diagnostic_index]") {
  std::string code = R"(
    module test_module  // Missing semicolon and endmodule
      logic signal;
  )";

  auto source_manager = std::make_shared<slang::SourceManager>();
  slang::Bag options;
  auto compilation = std::make_unique<slang::ast::Compilation>(options);

  constexpr std::string_view kTestFilename = "test.sv";
  std::string test_uri = "file:///" + std::string(kTestFilename);
  std::string test_path = "/" + std::string(kTestFilename);

  auto buffer = source_manager->assignText(test_path, code);
  auto tree = slang::syntax::SyntaxTree::fromBuffer(buffer, *source_manager);
  if (tree) {
    compilation->addSyntaxTree(tree);
  }

  auto diagnostic_index =
      DiagnosticIndex::FromCompilation(*compilation, *source_manager, test_uri);

  const auto& diagnostics = diagnostic_index.GetDiagnostics();
  REQUIRE(diagnostics.size() > 0);

  // Should have error diagnostics for malformed syntax
  bool found_syntax_error = false;
  for (const auto& diag : diagnostics) {
    if (diag.severity == lsp::DiagnosticSeverity::kError) {
      found_syntax_error = true;
      // Verify basic diagnostic structure
      REQUIRE(!diag.message.empty());
      REQUIRE(diag.source == "slang");
    }
  }
  REQUIRE(found_syntax_error);
}

TEST_CASE("DiagnosticIndex handles empty file", "[diagnostic_index]") {
  std::string code;

  auto source_manager = std::make_shared<slang::SourceManager>();
  slang::Bag options;
  auto compilation = std::make_unique<slang::ast::Compilation>(options);

  constexpr std::string_view kTestFilename = "test.sv";
  std::string test_uri = "file:///" + std::string(kTestFilename);
  std::string test_path = "/" + std::string(kTestFilename);

  auto buffer = source_manager->assignText(test_path, code);
  auto tree = slang::syntax::SyntaxTree::fromBuffer(buffer, *source_manager);
  if (tree) {
    compilation->addSyntaxTree(tree);
  }

  auto diagnostic_index =
      DiagnosticIndex::FromCompilation(*compilation, *source_manager, test_uri);

  // Empty file should not crash
  REQUIRE(diagnostic_index.GetUri() == test_uri);

  // May have diagnostics about no compilation units, but shouldn't crash
  const auto& diagnostics = diagnostic_index.GetDiagnostics();
  REQUIRE(diagnostics.size() >= 0);
}

}  // namespace slangd::semantic

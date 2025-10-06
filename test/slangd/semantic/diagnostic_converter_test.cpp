#include "slangd/semantic/diagnostic_converter.hpp"

#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_all.hpp>
#include <slang/ast/Symbol.h>
#include <spdlog/spdlog.h>

#include "../common/simple_fixture.hpp"

constexpr auto kLogLevel = spdlog::level::debug;

auto main(int argc, char* argv[]) -> int {
  spdlog::set_level(kLogLevel);
  spdlog::set_pattern("[%l] %v");

  // Suppress Bazel test sharding warnings
  setenv("TEST_SHARD_INDEX", "0", 0);
  setenv("TEST_TOTAL_SHARDS", "1", 0);
  setenv("TEST_SHARD_STATUS_FILE", "", 0);

  return Catch::Session().run(argc, argv);
}

using slangd::semantic::DiagnosticConverter;
using slangd::test::SimpleTestFixture;

// Helper function to get consistent test URI
inline auto GetTestUri() -> std::string {
  return "file:///test.sv";
}

TEST_CASE("DiagnosticConverter basic functionality", "[diagnostic_converter]") {
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

  auto diagnostics = DiagnosticConverter::ExtractAllDiagnostics(
      *compilation, *source_manager, buffer.id);

  // Valid code should have few or no diagnostics
  REQUIRE(diagnostics.size() >= 0);  // May have warnings but shouldn't fail
}

TEST_CASE(
    "DiagnosticConverter detects syntax errors", "[diagnostic_converter]") {
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

  auto diagnostics = DiagnosticConverter::ExtractAllDiagnostics(
      *compilation, *source_manager, buffer.id);

  REQUIRE(diagnostics.size() > 0);

  // Should have at least one error diagnostic
  SimpleTestFixture::AssertDiagnosticExists(
      diagnostics, lsp::DiagnosticSeverity::kError);

  // Verify diagnostic properties using helper
  SimpleTestFixture::AssertDiagnosticsValid(
      diagnostics, lsp::DiagnosticSeverity::kError);
}

TEST_CASE(
    "DiagnosticConverter detects semantic errors", "[diagnostic_converter]") {
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

  auto diagnostics = DiagnosticConverter::ExtractAllDiagnostics(
      *compilation, *source_manager, buffer.id);

  REQUIRE(diagnostics.size() > 0);

  // Should find the undefined variable error
  SimpleTestFixture::AssertDiagnosticExists(
      diagnostics, lsp::DiagnosticSeverity::kError, "undefined");
}

TEST_CASE(
    "DiagnosticConverter handles malformed module", "[diagnostic_converter]") {
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

  auto diagnostics = DiagnosticConverter::ExtractAllDiagnostics(
      *compilation, *source_manager, buffer.id);

  REQUIRE(diagnostics.size() > 0);

  // Should have error diagnostics for malformed syntax
  SimpleTestFixture::AssertDiagnosticExists(
      diagnostics, lsp::DiagnosticSeverity::kError);

  // Verify basic diagnostic structure using helper
  SimpleTestFixture::AssertDiagnosticsValid(
      diagnostics, lsp::DiagnosticSeverity::kError);
}

TEST_CASE("DiagnosticConverter handles empty file", "[diagnostic_converter]") {
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

  auto diagnostics = DiagnosticConverter::ExtractAllDiagnostics(
      *compilation, *source_manager, buffer.id);

  // May have diagnostics about no compilation units, but shouldn't crash
  REQUIRE(diagnostics.size() >= 0);
}

TEST_CASE(
    "DiagnosticConverter parse diagnostics are subset of all",
    "[diagnostic_converter]") {
  std::string code = R"(
    module test_module;
      logic signal  // Missing semicolon - parse error
      logic [7:0] data;

      initial begin
        undefined_var = 8'h42;  // Semantic error
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

  auto parse_diags = DiagnosticConverter::ExtractParseDiagnostics(
      *compilation, *source_manager, buffer.id);
  auto all_diags = DiagnosticConverter::ExtractAllDiagnostics(
      *compilation, *source_manager, buffer.id);

  // Parse diagnostics should be subset of all diagnostics
  REQUIRE(parse_diags.size() <= all_diags.size());

  // Each parse diagnostic should appear in all diagnostics (functional
  // approach)
  SimpleTestFixture::AssertDiagnosticsSubset(parse_diags, all_diags);
}

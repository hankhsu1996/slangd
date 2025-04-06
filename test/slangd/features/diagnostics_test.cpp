#include "slangd/features/diagnostics.hpp"

#include <string>

#include <asio.hpp>
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

// Helper function to setup source manager and extract diagnostics
std::vector<lsp::Diagnostic> ExtractDiagnosticsFromString(
    const std::string& source) {
  const std::string filename = "test.sv";
  auto source_manager = std::make_shared<slang::SourceManager>();
  auto syntax_tree =
      slang::syntax::SyntaxTree::fromText(source, *source_manager, filename);

  std::shared_ptr<slang::ast::Compilation> compilation;
  compilation = std::make_shared<slang::ast::Compilation>();
  compilation->addSyntaxTree(syntax_tree);

  std::string uri = "file://" + filename;
  slang::DiagnosticEngine diag_engine(*source_manager);
  return slangd::GetDocumentDiagnostics(
      syntax_tree, compilation, source_manager, diag_engine, uri);
}

// Helper function that allows specifying a custom URI
std::vector<lsp::Diagnostic> ExtractDiagnosticsFromStringWithURI(
    const std::string& source, const std::string& uri) {
  auto source_manager = std::make_shared<slang::SourceManager>();
  auto syntax_tree =
      slang::syntax::SyntaxTree::fromText(source, *source_manager, uri);

  auto compilation = std::make_shared<slang::ast::Compilation>();
  compilation->addSyntaxTree(syntax_tree);

  slang::DiagnosticEngine diag_engine(*source_manager);
  return slangd::GetDocumentDiagnostics(
      syntax_tree, compilation, source_manager, diag_engine, uri);
}

TEST_CASE(
    "ExtractSyntaxDiagnostics finds basic syntax error", "[diagnostics]") {
  // Missing semicolon after wire declaration
  std::string code = R"(
    module test_module;
      wire x    // missing semicolon
    endmodule
  )";

  auto diagnostics = ExtractDiagnosticsFromString(code);

  REQUIRE(!diagnostics.empty());
  REQUIRE(diagnostics[0].severity == lsp::DiagnosticSeverity::kError);
  REQUIRE(diagnostics[0].message == "expected ';'");
}

TEST_CASE("ExtractSemanticDiagnostics finds type error", "[diagnostics]") {
  // Type mismatch in assignment
  std::string code = R"(
    module test_module;
      logic [1:0] a;
      initial begin
        a = 3'b111;  // value too wide for target
      end
    endmodule
  )";

  auto diagnostics = ExtractDiagnosticsFromString(code);

  REQUIRE(!diagnostics.empty());
  REQUIRE(diagnostics[0].severity.value() == lsp::DiagnosticSeverity::kWarning);
  REQUIRE(
      diagnostics[0].message.find("implicit conversion") != std::string::npos);
}

TEST_CASE("Diagnostics finds undefined variable", "[diagnostics]") {
  std::string code = R"(
    module test_module;
      initial begin
        undefined_var = 1;  // variable not declared
      end
    endmodule
  )";

  auto diagnostics = ExtractDiagnosticsFromString(code);

  REQUIRE(!diagnostics.empty());
  REQUIRE(diagnostics[0].severity == lsp::DiagnosticSeverity::kError);
  REQUIRE(
      diagnostics[0].message.find("use of undeclared identifier") !=
      std::string::npos);
}

TEST_CASE("Diagnostics finds invalid module declaration", "[diagnostics]") {
  std::string code = R"(
    module test_module(
      input wire,  // port missing name
      output       // port missing type and name
    );
    endmodule
  )";

  auto diagnostics = ExtractDiagnosticsFromString(code);

  REQUIRE(diagnostics.size() >= 2);  // should have at least 2 errors
  REQUIRE(diagnostics[0].severity == lsp::DiagnosticSeverity::kError);
  REQUIRE(diagnostics[1].severity == lsp::DiagnosticSeverity::kError);
  REQUIRE(
      diagnostics[0].message.find("expected identifier") != std::string::npos);
}

TEST_CASE("Diagnostics reports correct error location", "[diagnostics]") {
  std::string code = R"(
    module test_module;
      wire a = 1'b0  // error on this line
      wire b;
    endmodule
  )";

  auto diagnostics = ExtractDiagnosticsFromString(code);

  REQUIRE(!diagnostics.empty());
  auto& diag = diagnostics[0];

  // Error should be on line 3 (1-based index)
  REQUIRE(diag.range.start.line == 2);  // 0-based line number
  REQUIRE(diag.range.end.line == 2);

  // Error should be at the end of the line (missing semicolon)
  REQUIRE(diag.range.start.character > 0);
  REQUIRE(diag.message == "expected ';'");
}

TEST_CASE("Diagnostics handles empty source", "[diagnostics]") {
  // Test with empty string
  std::string empty_code = "";
  auto empty_diagnostics = ExtractDiagnosticsFromString(empty_code);
  REQUIRE(empty_diagnostics.empty());  // Should have no errors

  // Test with only whitespace
  std::string whitespace_code = "   \n  \t  \n";
  auto whitespace_diagnostics = ExtractDiagnosticsFromString(whitespace_code);
  REQUIRE(whitespace_diagnostics.empty());  // Should have no errors
}

TEST_CASE(
    "Diagnostics reports multiple errors in different locations",
    "[diagnostics]") {
  std::string code = R"(
    module test_module
      wire a = 1'b0  // missing semicolon, error #1
      wire b        // missing semicolon, error #2

      initial begin
        x = 1;      // undefined variable, error #3
      end
    endmodule       // missing semicolon after module, error #4
  )";

  auto diagnostics = ExtractDiagnosticsFromString(code);

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
}

TEST_CASE("Diagnostics filters by correct URI", "[diagnostics]") {
  std::string code = R"(
    module test_module;
      wire x    // missing semicolon
    endmodule
  )";

  // Create source with a specific filename
  const std::string filename = "test.sv";
  auto source_manager = std::make_shared<slang::SourceManager>();
  auto syntax_tree =
      slang::syntax::SyntaxTree::fromText(code, *source_manager, filename);

  auto compilation = std::make_shared<slang::ast::Compilation>();
  compilation->addSyntaxTree(syntax_tree);
  slang::DiagnosticEngine diag_engine(*source_manager);

  // Test with matching URI
  std::string correct_uri = "file://" + filename;
  auto correct_diagnostics = slangd::GetDocumentDiagnostics(
      syntax_tree, compilation, source_manager, diag_engine, correct_uri);
  REQUIRE(!correct_diagnostics.empty());

  // Test with non-matching URI - should filter out diagnostics
  std::string wrong_uri = "file://other.sv";
  auto wrong_diagnostics = slangd::GetDocumentDiagnostics(
      syntax_tree, compilation, source_manager, diag_engine, wrong_uri);
  REQUIRE(wrong_diagnostics.empty());
}

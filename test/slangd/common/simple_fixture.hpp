#pragma once
#include <memory>
#include <optional>
#include <string>

#include <slang/text/SourceLocation.h>

#include "slangd/semantic/semantic_index.hpp"

namespace slangd::test {

class SimpleTestFixture {
 public:
  // Compile source and return semantic index (throws on compilation errors)
  auto CompileSource(const std::string& code)
      -> std::unique_ptr<semantic::SemanticIndex>;

  // Compile source and return diagnostics (does not throw on errors)
  auto CompileSourceAndGetDiagnostics(const std::string& code)
      -> std::vector<lsp::Diagnostic>;

  // Find symbol location in source by name (must be unique)
  auto FindSymbol(const std::string& code, const std::string& name)
      -> slang::SourceLocation;

  // Get definition range for symbol at location
  static auto GetDefinitionRange(
      semantic::SemanticIndex& index, slang::SourceLocation loc)
      -> std::optional<slang::SourceRange>;

  // High-level API for clean go-to-definition testing

  // Find all occurrences of a symbol in source code (ordered by appearance)
  auto FindAllOccurrences(
      const std::string& code, const std::string& symbol_name)
      -> std::vector<slang::SourceLocation>;

  // Assert that go-to-definition works: reference at ref_index points to
  // definition at def_index
  void AssertGoToDefinition(
      semantic::SemanticIndex& index, const std::string& code,
      const std::string& symbol_name, size_t reference_index,
      size_t definition_index);

  // Assert that a reference was captured by the semantic index
  void AssertReferenceExists(
      semantic::SemanticIndex& index, const std::string& code,
      const std::string& symbol_name, size_t reference_index);

  // Assert that index contains all the specified symbols
  static void AssertContainsSymbols(
      semantic::SemanticIndex& index,
      const std::vector<std::string>& expected_symbols);

  // Assert that a document symbol with specific name exists
  static void AssertDocumentSymbolExists(
      const std::vector<lsp::DocumentSymbol>& symbols,
      const std::string& symbol_name, lsp::SymbolKind expected_kind);

  // Assert that a diagnostic matching the criteria exists
  static void AssertDiagnosticExists(
      const std::vector<lsp::Diagnostic>& diagnostics,
      lsp::DiagnosticSeverity severity,
      const std::string& message_substring = "");

  // Assert that all diagnostics in subset are found in superset
  static void AssertDiagnosticsSubset(
      const std::vector<lsp::Diagnostic>& subset,
      const std::vector<lsp::Diagnostic>& superset);

  // Assert that all diagnostics with given severity have valid properties
  static void AssertDiagnosticsValid(
      const std::vector<lsp::Diagnostic>& diagnostics,
      lsp::DiagnosticSeverity severity);

  // Assert that no error diagnostics exist (ignores warnings/info)
  static void AssertNoErrors(const std::vector<lsp::Diagnostic>& diagnostics);

  // Assert that an error diagnostic with message substring exists
  static void AssertError(
      const std::vector<lsp::Diagnostic>& diagnostics,
      const std::string& message_substring = "");

  // Assert that a symbol's definition range has expected length
  void AssertDefinitionRangeLength(
      semantic::SemanticIndex& index, const std::string& code,
      const std::string& symbol_name, size_t expected_length);

  // Access to internal state for debugging
  auto SetupCompilation(const std::string& code) -> std::string;
  auto GetCompilation() -> slang::ast::Compilation& {
    return *compilation_;
  }

 private:
  std::shared_ptr<slang::SourceManager> source_manager_;
  std::unique_ptr<slang::ast::Compilation> compilation_;
  slang::BufferID buffer_id_;
};

}  // namespace slangd::test

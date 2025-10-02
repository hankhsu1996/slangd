#pragma once
#include <memory>
#include <optional>
#include <string>

#include <slang/text/SourceLocation.h>

#include "slangd/semantic/semantic_index.hpp"

namespace slangd::test {

class SimpleTestFixture {
 public:
  // Compile source and return semantic index
  auto CompileSource(const std::string& code)
      -> std::unique_ptr<semantic::SemanticIndex>;

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

  // Assert that a symbol's definition range has expected length
  void AssertDefinitionRangeLength(
      semantic::SemanticIndex& index, const std::string& code,
      const std::string& symbol_name, size_t expected_length);

 private:
  std::shared_ptr<slang::SourceManager> source_manager_;
  std::unique_ptr<slang::ast::Compilation> compilation_;
  slang::BufferID buffer_id_;
};

}  // namespace slangd::test

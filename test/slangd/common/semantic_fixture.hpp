#pragma once

#include <memory>
#include <regex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>
#include <lsp/basic.hpp>
#include <lsp/document_features.hpp>
#include <slang/ast/Compilation.h>
#include <slang/parsing/Preprocessor.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/text/SourceManager.h>
#include <slang/util/Bag.h>

#include "slangd/semantic/diagnostic_converter.hpp"
#include "slangd/semantic/semantic_index.hpp"
#include "slangd/utils/compilation_options.hpp"

namespace slangd::test {

// Base fixture for all semantic index tests
class SemanticTestFixture {
 public:
  using SemanticIndex = slangd::semantic::SemanticIndex;

  // Result struct that bundles index with its dependencies and diagnostics
  // Keeps source_manager and compilation alive (index stores pointers to them)
  // Always includes diagnostics - tests can ignore them if not needed
  struct TestIndexResult {
    std::unique_ptr<SemanticIndex> index;
    std::vector<lsp::Diagnostic> diagnostics;
    std::shared_ptr<slang::SourceManager> source_manager;
    std::unique_ptr<slang::ast::Compilation> compilation;
    std::string uri;
  };

  // Build semantic index and extract diagnostics (LSP-first approach)
  // Always returns diagnostics - tests can ignore them if not needed
  auto static BuildIndex(const std::string& source) -> TestIndexResult {
    constexpr std::string_view kTestFilename = "test.sv";

    // Use consistent URI/path format
    std::string test_uri = "file:///" + std::string(kTestFilename);
    std::string test_path = "/" + std::string(kTestFilename);

    auto options = utils::CreateLspCompilationOptions();

    auto source_manager = std::make_shared<slang::SourceManager>();
    auto buffer = source_manager->assignText(test_path, source);
    auto tree =
        slang::syntax::SyntaxTree::fromBuffer(buffer, *source_manager, options);

    auto compilation = std::make_unique<slang::ast::Compilation>(options);
    compilation->addSyntaxTree(tree);

    // Get buffer_id for semantic indexing
    auto buffer_id = buffer.id;

    // Build semantic index (triggers forceElaborate internally)
    auto result = SemanticIndex::FromCompilation(
        *compilation, *source_manager, test_uri, buffer_id);

    if (!result) {
      throw std::runtime_error(
          fmt::format(
              "BuildIndex: Failed to build semantic index: {}",
              result.error()));
    }

    auto index = std::move(*result);
    auto parse_diags = semantic::DiagnosticConverter::ExtractParseDiagnostics(
        *compilation, *source_manager, buffer_id);

    auto semantic_diags =
        semantic::DiagnosticConverter::ExtractCollectedDiagnostics(
            *compilation, *source_manager, buffer_id);

    // Combine diagnostics
    std::vector<lsp::Diagnostic> diagnostics;
    diagnostics.reserve(parse_diags.size() + semantic_diags.size());
    diagnostics.insert(
        diagnostics.end(), parse_diags.begin(), parse_diags.end());
    diagnostics.insert(
        diagnostics.end(), semantic_diags.begin(), semantic_diags.end());

    return TestIndexResult{
        .index = std::move(index),
        .diagnostics = std::move(diagnostics),
        .source_manager = std::move(source_manager),
        .compilation = std::move(compilation),
        .uri = std::move(test_uri)};
  }

  // Simple helper: convert byte offset to LSP position (ASCII-only for tests)
  static auto ConvertOffsetToLspPosition(
      const std::string& source, size_t offset) -> lsp::Position {
    int line = 0;
    size_t line_start = 0;

    for (size_t i = 0; i < offset; i++) {
      if (source[i] == '\n') {
        line++;
        line_start = i + 1;
      }
    }

    auto character = static_cast<int>(offset - line_start);
    return lsp::Position{.line = line, .character = character};
  }

  // Find position of text in source (LSP coordinates)
  // Simple ASCII-only conversion suitable for test code
  static auto FindLocation(const std::string& source, const std::string& text)
      -> lsp::Position {
    size_t offset = source.find(text);
    if (offset == std::string::npos) {
      throw std::runtime_error(
          fmt::format("FindLocation: Text '{}' not found in source", text));
    }

    return ConvertOffsetToLspPosition(source, offset);
  }

  // Find all LSP positions of a symbol in source code
  static auto FindAllOccurrences(
      const std::string& code, const std::string& symbol_name)
      -> std::vector<lsp::Position> {
    std::vector<lsp::Position> positions;
    std::string pattern = R"(\b)" + std::string(symbol_name) + R"(\b)";
    std::regex symbol_regex(pattern);

    auto begin = std::sregex_iterator(code.begin(), code.end(), symbol_regex);
    auto end = std::sregex_iterator();

    for (auto it = begin; it != end; ++it) {
      auto offset = static_cast<size_t>(it->position());
      positions.push_back(ConvertOffsetToLspPosition(code, offset));
    }

    if (positions.empty()) {
      throw std::runtime_error(
          fmt::format(
              "FindAllOccurrences: No occurrences of '{}' found", symbol_name));
    }

    return positions;
  }

  // Diagnostic assertion helpers (LSP-first, static methods)

  static void AssertDiagnosticExists(
      const std::vector<lsp::Diagnostic>& diagnostics,
      lsp::DiagnosticSeverity severity,
      const std::string& message_substring = "") {
    for (const auto& diagnostic : diagnostics) {
      if (diagnostic.severity == severity) {
        if (message_substring.empty() ||
            diagnostic.message.find(message_substring) != std::string::npos) {
          return;  // Found matching diagnostic
        }
      }
    }

    std::string error_msg = fmt::format(
        "AssertDiagnosticExists: No diagnostic found with severity");
    if (!message_substring.empty()) {
      error_msg +=
          fmt::format(" and message containing '{}'", message_substring);
    }
    throw std::runtime_error(error_msg);
  }

  static void AssertNoErrors(const std::vector<lsp::Diagnostic>& diagnostics) {
    auto error_diag = std::ranges::find_if(diagnostics, [](const auto& diag) {
      return diag.severity == lsp::DiagnosticSeverity::kError;
    });

    if (error_diag != diagnostics.end()) {
      throw std::runtime_error(
          fmt::format(
              "AssertNoErrors: Found unexpected error diagnostic: '{}'",
              error_diag->message));
    }
  }

  static void AssertError(
      const std::vector<lsp::Diagnostic>& diagnostics,
      const std::string& message_substring = "") {
    AssertDiagnosticExists(
        diagnostics, lsp::DiagnosticSeverity::kError, message_substring);
  }

  // Go-to-definition assertion helper (LSP-first)
  static void AssertGoToDefinition(
      SemanticIndex& index, const std::string& uri, const std::string& code,
      const std::string& symbol_name, size_t reference_index,
      size_t definition_index) {
    auto occurrences = FindAllOccurrences(code, symbol_name);

    if (reference_index >= occurrences.size()) {
      throw std::runtime_error(
          fmt::format(
              "AssertGoToDefinition: reference_index {} out of range for "
              "symbol '{}' (found {} occurrences)",
              reference_index, symbol_name, occurrences.size()));
    }

    if (definition_index >= occurrences.size()) {
      throw std::runtime_error(
          fmt::format(
              "AssertGoToDefinition: definition_index {} out of range for "
              "symbol '{}' (found {} occurrences)",
              definition_index, symbol_name, occurrences.size()));
    }

    const auto& reference_pos = occurrences[reference_index];
    const auto& expected_def_pos = occurrences[definition_index];

    // Perform go-to-definition lookup with LSP coordinates
    auto actual_def_range = index.LookupDefinitionAt(uri, reference_pos);

    if (!actual_def_range.has_value()) {
      throw std::runtime_error(
          fmt::format(
              "AssertGoToDefinition: LookupDefinitionAt failed for symbol '{}' "
              "at reference_index {} (position {}:{})",
              symbol_name, reference_index, reference_pos.line,
              reference_pos.character));
    }

    // Verify exact range: must start at expected location and span exactly
    // the symbol name length
    const auto& actual_start = actual_def_range->range.start;
    const auto& actual_end = actual_def_range->range.end;

    if (actual_start.line != expected_def_pos.line ||
        actual_start.character != expected_def_pos.character) {
      throw std::runtime_error(
          fmt::format(
              "AssertGoToDefinition: definition start mismatch for symbol "
              "'{}'. Expected ({}:{}), got ({}:{})",
              symbol_name, expected_def_pos.line, expected_def_pos.character,
              actual_start.line, actual_start.character));
    }

    auto actual_length = actual_end.character - actual_start.character;
    if (actual_length != static_cast<int>(symbol_name.length())) {
      throw std::runtime_error(
          fmt::format(
              "AssertGoToDefinition: definition length mismatch for symbol "
              "'{}'. Expected length {}, got {}",
              symbol_name, symbol_name.length(), actual_length));
    }
  }

  // Document symbol helpers

  static void AssertContainsSymbols(
      SemanticIndex& index, const std::vector<std::string>& expected_symbols) {
    const auto& semantic_entries = index.GetSemanticEntries();
    std::vector<std::string> found_symbol_names;

    for (const auto& entry : semantic_entries) {
      found_symbol_names.push_back(entry.name);
    }

    for (const auto& expected : expected_symbols) {
      if (!std::ranges::contains(found_symbol_names, expected)) {
        throw std::runtime_error(
            fmt::format(
                "AssertContainsSymbols: Expected symbol '{}' not found in "
                "index",
                expected));
      }
    }
  }

  static void AssertDocumentSymbolExists(
      const std::vector<lsp::DocumentSymbol>& symbols,
      const std::string& symbol_name, lsp::SymbolKind expected_kind) {
    std::function<bool(const std::vector<lsp::DocumentSymbol>&)> search_symbols;
    search_symbols = [&](const std::vector<lsp::DocumentSymbol>& syms) -> bool {
      return std::ranges::any_of(syms, [&](const auto& symbol) -> bool {
        return (symbol.name == symbol_name && symbol.kind == expected_kind) ||
               (symbol.children.has_value() &&
                search_symbols(*symbol.children));
      });
    };

    if (!search_symbols(symbols)) {
      throw std::runtime_error(
          fmt::format(
              "AssertDocumentSymbolExists: Symbol '{}' with expected kind not "
              "found",
              symbol_name));
    }
  }
};

}  // namespace slangd::test

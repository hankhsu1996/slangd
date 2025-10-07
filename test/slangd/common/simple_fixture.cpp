#include "simple_fixture.hpp"

#include <algorithm>
#include <functional>
#include <regex>
#include <stdexcept>

#include <fmt/format.h>
#include <slang/ast/Compilation.h>
#include <slang/diagnostics/DiagnosticEngine.h>
#include <slang/parsing/Preprocessor.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/text/SourceManager.h>
#include <slang/util/Bag.h>

#include "slangd/semantic/diagnostic_converter.hpp"

namespace slangd::test {

// Helper to create LSP-style compilation options
// This matches the configuration used in OverlaySession and GlobalCatalog
static auto CreateLspCompilationOptions() -> slang::Bag {
  slang::Bag options;

  // Disable implicit net declarations for stricter diagnostics
  slang::parsing::PreprocessorOptions pp_options;
  pp_options.initialDefaultNetType = slang::parsing::TokenKind::Unknown;
  options.set(pp_options);

  slang::ast::CompilationOptions comp_options;
  comp_options.flags |= slang::ast::CompilationFlags::LintMode;
  comp_options.flags |= slang::ast::CompilationFlags::LanguageServerMode;
  comp_options.errorLimit = 0;  // Unlimited errors for LSP
  options.set(comp_options);
  return options;
}

auto SimpleTestFixture::SetupCompilation(const std::string& code)
    -> std::string {
  constexpr std::string_view kTestFilename = "test.sv";

  // Use consistent URI/path format
  std::string test_uri = "file:///" + std::string(kTestFilename);
  std::string test_path = "/" + std::string(kTestFilename);

  source_manager_ = std::make_shared<slang::SourceManager>();
  auto buffer = source_manager_->assignText(test_path, code);
  buffer_id_ = buffer.id;

  // Use LSP-style compilation options (must be created before SyntaxTree)
  auto options = CreateLspCompilationOptions();
  auto tree =
      slang::syntax::SyntaxTree::fromBuffer(buffer, *source_manager_, options);

  compilation_ = std::make_unique<slang::ast::Compilation>(options);
  compilation_->addSyntaxTree(tree);

  return test_uri;
}

auto SimpleTestFixture::CompileSource(const std::string& code)
    -> std::unique_ptr<semantic::SemanticIndex> {
  auto test_uri = SetupCompilation(code);

  // Check for compilation errors that would make AST invalid
  auto diagnostics = compilation_->getAllDiagnostics();
  std::string error_messages;
  slang::DiagnosticEngine diag_engine(*source_manager_);
  for (const auto& diag : diagnostics) {
    if (diag.isError()) {
      if (!error_messages.empty()) {
        error_messages += "\n";
      }
      error_messages += diag_engine.formatMessage(diag);
    }
  }
  if (!error_messages.empty()) {
    throw std::runtime_error(
        fmt::format(
            "CompileSource: Compilation failed with errors:\n{}",
            error_messages));
  }

  auto index = semantic::SemanticIndex::FromCompilation(
      *compilation_, *source_manager_, test_uri);

  // Validate that compilation succeeded
  if (!index) {
    throw std::runtime_error("CompileSource: Failed to create semantic index");
  }

  return index;
}

auto SimpleTestFixture::CompileSourceAndGetDiagnostics(const std::string& code)
    -> std::vector<lsp::Diagnostic> {
  auto test_uri = SetupCompilation(code);

  // Use production code path - SemanticIndex::FromCompilation() calls
  // forceElaborate() internally, populating diagMap
  auto semantic_index = semantic::SemanticIndex::FromCompilation(
      *compilation_, *source_manager_, test_uri);

  // Extract both parse and semantic diagnostics (same as production)
  auto parse_diags = semantic::DiagnosticConverter::ExtractParseDiagnostics(
      *compilation_, *source_manager_, buffer_id_);

  auto semantic_diags =
      semantic::DiagnosticConverter::ExtractCollectedDiagnostics(
          *compilation_, *source_manager_, buffer_id_);

  // Combine both
  std::vector<lsp::Diagnostic> result;
  result.reserve(parse_diags.size() + semantic_diags.size());
  result.insert(result.end(), parse_diags.begin(), parse_diags.end());
  result.insert(result.end(), semantic_diags.begin(), semantic_diags.end());

  return result;
}

auto SimpleTestFixture::FindSymbol(
    const std::string& code, const std::string& name) -> slang::SourceLocation {
  size_t offset = code.find(name);
  if (offset == std::string::npos) {
    throw std::runtime_error(
        fmt::format("FindSymbol: Symbol '{}' not found in source", name));
  }

  // Detect ambiguous symbol names early
  size_t second_occurrence = code.find(name, offset + 1);
  if (second_occurrence != std::string::npos) {
    throw std::runtime_error(
        fmt::format(
            "FindSymbol: Ambiguous symbol '{}' found at multiple locations. "
            "Use unique descriptive names in test code.",
            name));
  }

  return slang::SourceLocation{buffer_id_, offset};
}

auto SimpleTestFixture::GetDefinitionRange(
    semantic::SemanticIndex& index, slang::SourceLocation loc)
    -> std::optional<slang::SourceRange> {
  auto def_loc = index.LookupDefinitionAt(loc);
  if (!def_loc) {
    return std::nullopt;
  }
  // Return same_file_range for SimpleTestFixture (no cross-file)
  return def_loc->same_file_range;
}

auto SimpleTestFixture::FindAllOccurrences(
    const std::string& code, const std::string& symbol_name)
    -> std::vector<slang::SourceLocation> {
  std::vector<slang::SourceLocation> occurrences;

  // Create regex pattern for complete identifier match
  // \b = word boundary, ensures we match complete identifiers only
  std::string pattern = R"(\b)" + symbol_name + R"(\b)";
  std::regex symbol_regex(pattern);

  // Use sregex_iterator for elegant iteration over all matches
  auto begin = std::sregex_iterator(code.begin(), code.end(), symbol_regex);
  auto end = std::sregex_iterator();

  for (auto it = begin; it != end; ++it) {
    const std::smatch& match = *it;
    occurrences.emplace_back(buffer_id_, match.position());
  }

  if (occurrences.empty()) {
    throw std::runtime_error(
        fmt::format(
            "FindAllOccurrences: No occurrences of '{}' found", symbol_name));
  }

  return occurrences;
}

void SimpleTestFixture::AssertGoToDefinition(
    semantic::SemanticIndex& index, const std::string& code,
    const std::string& symbol_name, size_t reference_index,
    size_t definition_index) {
  auto occurrences = FindAllOccurrences(code, symbol_name);

  if (reference_index >= occurrences.size()) {
    throw std::runtime_error(
        fmt::format(
            "AssertGoToDefinition: reference_index {} out of range for symbol "
            "'{}' (found {} occurrences)",
            reference_index, symbol_name, occurrences.size()));
  }

  if (definition_index >= occurrences.size()) {
    throw std::runtime_error(
        fmt::format(
            "AssertGoToDefinition: definition_index {} out of range for symbol "
            "'{}' (found {} occurrences)",
            definition_index, symbol_name, occurrences.size()));
  }

  auto reference_loc = occurrences[reference_index];
  auto expected_def_loc = occurrences[definition_index];

  // Perform go-to-definition lookup
  auto actual_def_range = index.LookupDefinitionAt(reference_loc);

  if (!actual_def_range.has_value()) {
    throw std::runtime_error(
        fmt::format(
            "AssertGoToDefinition: LookupDefinitionAt failed for symbol '{}' "
            "at reference_index {}",
            symbol_name, reference_index));
  }

  // Verify exact range: must start at expected location and span exactly the
  // symbol name length
  auto expected_start = expected_def_loc.offset();
  auto expected_end = expected_start + symbol_name.length();

  // Extract range from DefinitionLocation (should be same_file_range for
  // SimpleTestFixture)
  if (!actual_def_range->same_file_range.has_value()) {
    throw std::runtime_error(
        fmt::format(
            "AssertGoToDefinition: Expected same_file_range for symbol '{}', "
            "got cross_file instead",
            symbol_name));
  }
  auto actual_start = actual_def_range->same_file_range->start().offset();
  auto actual_end = actual_def_range->same_file_range->end().offset();

  if (actual_start != expected_start || actual_end != expected_end) {
    throw std::runtime_error(
        fmt::format(
            "AssertGoToDefinition: definition range mismatch for symbol '{}'. "
            "Expected range [{}, {}), got [{}, {})",
            symbol_name, expected_start, expected_end, actual_start,
            actual_end));
  }
}

void SimpleTestFixture::AssertReferenceExists(
    semantic::SemanticIndex& index, const std::string& code,
    const std::string& symbol_name, size_t reference_index) {
  auto occurrences = FindAllOccurrences(code, symbol_name);

  if (reference_index >= occurrences.size()) {
    throw std::runtime_error(
        fmt::format(
            "AssertReferenceExists: reference_index {} out of range for symbol "
            "'{}' (found {} occurrences)",
            reference_index, symbol_name, occurrences.size()));
  }

  auto reference_loc = occurrences[reference_index];

  // Check that the reference location produces a valid go-to-definition result
  auto def_range = index.LookupDefinitionAt(reference_loc);

  if (!def_range.has_value()) {
    throw std::runtime_error(
        fmt::format(
            "AssertReferenceExists: reference not found for symbol '{}' at "
            "reference_index {}",
            symbol_name, reference_index));
  }
}

void SimpleTestFixture::AssertContainsSymbols(
    semantic::SemanticIndex& index,
    const std::vector<std::string>& expected_symbols) {
  const auto& semantic_entries = index.GetSemanticEntries();
  std::vector<std::string> found_symbol_names;

  for (const auto& entry : semantic_entries) {
    found_symbol_names.push_back(entry.name);
  }

  for (const auto& expected : expected_symbols) {
    if (!std::ranges::contains(found_symbol_names, expected)) {
      throw std::runtime_error(
          fmt::format(
              "AssertContainsSymbols: Expected symbol '{}' not found in index",
              expected));
    }
  }
}

void SimpleTestFixture::AssertDocumentSymbolExists(
    const std::vector<lsp::DocumentSymbol>& symbols,
    const std::string& symbol_name, lsp::SymbolKind expected_kind) {
  std::function<bool(const std::vector<lsp::DocumentSymbol>&)> search_symbols;
  search_symbols = [&](const std::vector<lsp::DocumentSymbol>& syms) -> bool {
    return std::ranges::any_of(syms, [&](const auto& symbol) -> bool {
      return (symbol.name == symbol_name && symbol.kind == expected_kind) ||
             (symbol.children.has_value() && search_symbols(*symbol.children));
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

void SimpleTestFixture::AssertDiagnosticExists(
    const std::vector<lsp::Diagnostic>& diagnostics,
    lsp::DiagnosticSeverity severity, const std::string& message_substring) {
  for (const auto& diagnostic : diagnostics) {
    if (diagnostic.severity == severity) {
      if (message_substring.empty() ||
          diagnostic.message.find(message_substring) != std::string::npos) {
        return;  // Found matching diagnostic
      }
    }
  }

  std::string error_msg =
      fmt::format("AssertDiagnosticExists: No diagnostic found with severity");
  if (!message_substring.empty()) {
    error_msg += fmt::format(" and message containing '{}'", message_substring);
  }
  throw std::runtime_error(error_msg);
}

void SimpleTestFixture::AssertDefinitionRangeLength(
    semantic::SemanticIndex& index, const std::string& code,
    const std::string& symbol_name, size_t expected_length) {
  auto symbol_location = FindSymbol(code, symbol_name);
  if (!symbol_location.valid()) {
    throw std::runtime_error(
        fmt::format(
            "AssertDefinitionRangeLength: Symbol '{}' not found", symbol_name));
  }

  auto definition_range = GetDefinitionRange(index, symbol_location);
  if (!definition_range.has_value()) {
    throw std::runtime_error(
        fmt::format(
            "AssertDefinitionRangeLength: No definition range found for '{}'",
            symbol_name));
  }

  auto actual_length =
      definition_range->end().offset() - definition_range->start().offset();

  if (actual_length != expected_length) {
    throw std::runtime_error(
        fmt::format(
            "AssertDefinitionRangeLength: Expected length {} but got {} for "
            "'{}'",
            expected_length, actual_length, symbol_name));
  }
}

void SimpleTestFixture::AssertDiagnosticsSubset(
    const std::vector<lsp::Diagnostic>& subset,
    const std::vector<lsp::Diagnostic>& superset) {
  // Helper to check if two diagnostics match
  auto diagnostics_match = [](const lsp::Diagnostic& a,
                              const lsp::Diagnostic& b) -> bool {
    return a.message == b.message && a.range.start.line == b.range.start.line &&
           a.range.start.character == b.range.start.character;
  };

  // Check that all subset diagnostics appear in superset using ranges
  bool all_found = std::ranges::all_of(subset, [&](const auto& sub_diag) {
    return std::ranges::any_of(superset, [&](const auto& super_diag) {
      return diagnostics_match(sub_diag, super_diag);
    });
  });

  if (!all_found) {
    throw std::runtime_error(
        "AssertDiagnosticsSubset: Not all subset diagnostics found in "
        "superset");
  }
}

void SimpleTestFixture::AssertDiagnosticsValid(
    const std::vector<lsp::Diagnostic>& diagnostics,
    lsp::DiagnosticSeverity severity) {
  // Use ranges to find first diagnostic with matching severity
  auto matching_diag = std::ranges::find_if(
      diagnostics,
      [severity](const auto& diag) { return diag.severity == severity; });

  if (matching_diag == diagnostics.end()) {
    throw std::runtime_error(
        "AssertDiagnosticsValid: No diagnostic found with specified severity");
  }

  // Validate properties of the found diagnostic
  if (matching_diag->range.start.line < 0 ||
      matching_diag->range.start.character < 0) {
    throw std::runtime_error(
        "AssertDiagnosticsValid: Diagnostic has invalid range");
  }

  if (matching_diag->message.empty()) {
    throw std::runtime_error(
        "AssertDiagnosticsValid: Diagnostic has empty message");
  }

  if (matching_diag->source != "slang") {
    throw std::runtime_error(
        fmt::format(
            "AssertDiagnosticsValid: Expected source 'slang', got '{}'",
            matching_diag->source.value_or("")));
  }
}

void SimpleTestFixture::AssertNoErrors(
    const std::vector<lsp::Diagnostic>& diagnostics) {
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

void SimpleTestFixture::AssertError(
    const std::vector<lsp::Diagnostic>& diagnostics,
    const std::string& message_substring) {
  AssertDiagnosticExists(
      diagnostics, lsp::DiagnosticSeverity::kError, message_substring);
}

}  // namespace slangd::test

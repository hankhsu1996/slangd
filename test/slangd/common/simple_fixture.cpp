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
#include <slang/text/SourceLocation.h>
#include <slang/text/SourceManager.h>
#include <slang/util/Bag.h>

#include "slangd/semantic/diagnostic_converter.hpp"
#include "slangd/utils/conversion.hpp"

namespace slangd::test {

// Helper to create LSP-style compilation options
// This matches the configuration used in OverlaySession and PreambleManager
static auto CreateLspCompilationOptions(bool enable_lint_mode = true)
    -> slang::Bag {
  slang::Bag options;

  // Disable implicit net declarations for stricter diagnostics
  slang::parsing::PreprocessorOptions pp_options;
  pp_options.initialDefaultNetType = slang::parsing::TokenKind::Unknown;
  options.set(pp_options);

  // Configure lexer options for compatibility
  slang::parsing::LexerOptions lexer_options;
  // Enable legacy protection directives for compatibility with older codebases
  lexer_options.enableLegacyProtect = true;
  options.set(lexer_options);

  slang::ast::CompilationOptions comp_options;
  if (enable_lint_mode) {
    comp_options.flags |= slang::ast::CompilationFlags::LintMode;
  }
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

  // Use LSP-style compilation options WITHOUT LintMode
  // LintMode marks all scopes as uninstantiated which suppresses diagnostics
  auto options = CreateLspCompilationOptions(/* enable_lint_mode= */ false);
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

auto SimpleTestFixture::FindAllOccurrences(
    const std::string& code, const std::string& symbol_name)
    -> std::vector<LspOccurrence> {
  std::vector<LspOccurrence> occurrences;

  // Create regex pattern for complete identifier match
  // \b = word boundary, ensures we match complete identifiers only
  std::string pattern = R"(\b)" + symbol_name + R"(\b)";
  std::regex symbol_regex(pattern);

  // Use sregex_iterator for elegant iteration over all matches
  auto begin = std::sregex_iterator(code.begin(), code.end(), symbol_regex);
  auto end = std::sregex_iterator();

  // Use Slang conversion for correctness (UTF-16 code units)
  // The key improvement: test *assertions* still operate purely on LSP
  // coordinates
  for (auto it = begin; it != end; ++it) {
    const std::smatch& match = *it;
    auto offset = static_cast<size_t>(match.position());
    auto slang_loc = slang::SourceLocation{buffer_id_, offset};

    auto lsp_location =
        slangd::ConvertSlangLocationToLspLocation(slang_loc, *source_manager_);
    auto lsp_position =
        slangd::ConvertSlangLocationToLspPosition(slang_loc, *source_manager_);

    occurrences.push_back(
        LspOccurrence{.uri = lsp_location.uri, .position = lsp_position});
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

  const auto& reference_occ = occurrences[reference_index];
  const auto& expected_def_occ = occurrences[definition_index];

  // Perform go-to-definition lookup with LSP coordinates
  auto actual_def_range =
      index.LookupDefinitionAt(reference_occ.uri, reference_occ.position);

  if (!actual_def_range.has_value()) {
    throw std::runtime_error(
        fmt::format(
            "AssertGoToDefinition: LookupDefinitionAt failed for symbol '{}' "
            "at reference_index {}",
            symbol_name, reference_index));
  }

  // Verify exact range: must start at expected location and span exactly the
  // symbol name length
  const auto& actual_start = actual_def_range->range.start;
  const auto& actual_end = actual_def_range->range.end;
  const auto& expected_start = expected_def_occ.position;

  if (actual_start.line != expected_start.line ||
      actual_start.character != expected_start.character) {
    throw std::runtime_error(
        fmt::format(
            "AssertGoToDefinition: definition start mismatch for symbol '{}'. "
            "Expected ({}:{}), got ({}:{})",
            symbol_name, expected_start.line, expected_start.character,
            actual_start.line, actual_start.character));
  }

  auto actual_length = actual_end.character - actual_start.character;
  if (actual_length != static_cast<int>(symbol_name.length())) {
    throw std::runtime_error(
        fmt::format(
            "AssertGoToDefinition: definition length mismatch for symbol '{}'. "
            "Expected length {}, got {}",
            symbol_name, symbol_name.length(), actual_length));
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

  const auto& reference_occ = occurrences[reference_index];

  // Check that the reference location produces a valid go-to-definition result
  auto def_range =
      index.LookupDefinitionAt(reference_occ.uri, reference_occ.position);

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

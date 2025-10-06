#include "slangd/semantic/diagnostic_converter.hpp"

#include <slang/diagnostics/DiagnosticEngine.h>
#include <slang/diagnostics/Diagnostics.h>

#include "slangd/utils/conversion.hpp"

namespace slangd::semantic {

auto DiagnosticConverter::ExtractParseDiagnostics(
    slang::ast::Compilation& compilation,
    const slang::SourceManager& source_manager, slang::BufferID main_buffer_id,
    std::shared_ptr<spdlog::logger> logger) -> std::vector<lsp::Diagnostic> {
  if (!logger) {
    logger = spdlog::default_logger();
  }

  auto slang_diagnostics = compilation.getParseDiagnostics();
  auto diagnostics =
      ExtractDiagnostics(slang_diagnostics, source_manager, main_buffer_id);
  return FilterAndModifyDiagnostics(diagnostics);
}

auto DiagnosticConverter::ExtractAllDiagnostics(
    slang::ast::Compilation& compilation,
    const slang::SourceManager& source_manager, slang::BufferID main_buffer_id,
    std::shared_ptr<spdlog::logger> logger) -> std::vector<lsp::Diagnostic> {
  if (!logger) {
    logger = spdlog::default_logger();
  }

  auto slang_diagnostics = compilation.getAllDiagnostics();
  auto diagnostics =
      ExtractDiagnostics(slang_diagnostics, source_manager, main_buffer_id);
  return FilterAndModifyDiagnostics(diagnostics);
}

auto DiagnosticConverter::ExtractDiagnostics(
    const slang::Diagnostics& slang_diagnostics,
    const slang::SourceManager& source_manager, slang::BufferID main_buffer_id)
    -> std::vector<lsp::Diagnostic> {
  // Create a diagnostic engine using the source manager
  slang::DiagnosticEngine diagnostic_engine(source_manager);

  // Disable unnamed-generate warnings by default
  std::vector<std::string> warning_options = {"none", "default"};
  diagnostic_engine.setWarningOptions(warning_options);

  return ConvertSlangDiagnosticsToLsp(
      slang_diagnostics, source_manager, diagnostic_engine, main_buffer_id);
}

auto DiagnosticConverter::FilterAndModifyDiagnostics(
    std::vector<lsp::Diagnostic> diagnostics) -> std::vector<lsp::Diagnostic> {
  std::vector<lsp::Diagnostic> result;
  result.reserve(diagnostics.size());

  for (auto& diag : diagnostics) {
    // Check for diagnostics to completely exclude
    if (diag.code == "InfoTask") {
      continue;
    }

    // Check for diagnostics to demote and enhance
    if (diag.code == "CouldNotOpenIncludeFile") {
      diag.severity = lsp::DiagnosticSeverity::kWarning;

      // Replace original message with more helpful one
      std::string original_path;
      size_t quote_pos = diag.message.find('\'');
      if (quote_pos != std::string::npos) {
        size_t end_quote = diag.message.find('\'', quote_pos + 1);
        if (end_quote != std::string::npos) {
          original_path =
              diag.message.substr(quote_pos, end_quote - quote_pos + 1);
        }
      }

      if (!original_path.empty()) {
        diag.message = "Cannot find include file " + original_path;
      }

      diag.message +=
          " (Consider configuring include directories in a .slangd file)";
    } else if (diag.code == "UnknownDirective") {
      diag.severity = lsp::DiagnosticSeverity::kWarning;
      diag.message += " (Add defines in .slangd file if needed)";
    }

    result.push_back(diag);
  }

  return result;
}

auto DiagnosticConverter::ConvertSlangDiagnosticsToLsp(
    const slang::Diagnostics& slang_diagnostics,
    const slang::SourceManager& source_manager,
    const slang::DiagnosticEngine& diag_engine, slang::BufferID main_buffer_id)
    -> std::vector<lsp::Diagnostic> {
  std::vector<lsp::Diagnostic> result;

  for (const auto& diag : slang_diagnostics) {
    // Fast O(1) BufferID comparison - skip diagnostics not in main file
    if (!diag.location || diag.location.buffer() != main_buffer_id) {
      continue;
    }

    // Create the LSP diagnostic
    lsp::Diagnostic lsp_diag;

    // Get severity from the diagnostic engine
    lsp_diag.severity = ConvertDiagnosticSeverityToLsp(
        diag_engine.getSeverity(diag.code, diag.location));

    // Format message using the diagnostic engine
    lsp_diag.message = diag_engine.formatMessage(diag);

    if (diag.ranges.size() > 0) {
      // Explicitly select the first range
      lsp_diag.range =
          ConvertSlangRangeToLspRange(diag.ranges[0], source_manager);
    }
    // Convert location to range
    else if (diag.location) {
      lsp_diag.range =
          ConvertSlangLocationToLspRange(diag.location, source_manager);
    }
    // Fallback to an empty range at the start of the file
    else {
      lsp_diag.range = lsp::Range{
          .start = lsp::Position{.line = 0, .character = 0},
          .end = lsp::Position{.line = 0, .character = 0}};
    }

    // Add optional code and source fields
    lsp_diag.code = toString(diag.code);
    lsp_diag.source = "slang";

    result.push_back(lsp_diag);
  }

  return result;
}

// Helper functions - static members for conversion utilities
auto DiagnosticConverter::ConvertDiagnosticSeverityToLsp(
    slang::DiagnosticSeverity severity) -> lsp::DiagnosticSeverity {
  switch (severity) {
    case slang::DiagnosticSeverity::Ignored:
      return lsp::DiagnosticSeverity::kHint;
    case slang::DiagnosticSeverity::Note:
      return lsp::DiagnosticSeverity::kInformation;
    case slang::DiagnosticSeverity::Warning:
      return lsp::DiagnosticSeverity::kWarning;
    case slang::DiagnosticSeverity::Error:
      return lsp::DiagnosticSeverity::kError;
    case slang::DiagnosticSeverity::Fatal:
      return lsp::DiagnosticSeverity::kError;
  }
}

}  // namespace slangd::semantic

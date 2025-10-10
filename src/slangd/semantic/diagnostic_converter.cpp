#include "slangd/semantic/diagnostic_converter.hpp"

#include <string_view>

#include <slang/diagnostics/DiagnosticEngine.h>
#include <slang/diagnostics/Diagnostics.h>

#include "slangd/services/global_catalog.hpp"
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
  return FilterDiagnostics(diagnostics);
}

auto DiagnosticConverter::ExtractCollectedDiagnostics(
    slang::ast::Compilation& compilation,
    const slang::SourceManager& source_manager, slang::BufferID main_buffer_id,
    const services::GlobalCatalog* global_catalog)
    -> std::vector<lsp::Diagnostic> {
  // Get diagnostics from diagMap without triggering elaboration
  auto slang_diagnostics = compilation.getCollectedDiagnostics();
  auto diagnostics =
      ExtractDiagnostics(slang_diagnostics, source_manager, main_buffer_id);
  return FilterDiagnostics(diagnostics, global_catalog);
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

auto DiagnosticConverter::FilterDiagnostics(
    std::vector<lsp::Diagnostic> diagnostics,
    const services::GlobalCatalog* global_catalog)
    -> std::vector<lsp::Diagnostic> {
  std::vector<lsp::Diagnostic> result;

  for (const auto& diag : diagnostics) {
    // Filter out InfoTask diagnostics (not relevant for LSP clients)
    if (diag.code == "InfoTask") {
      continue;
    }

    // Filter UnknownModule if GlobalCatalog has the definition
    // This handles OverlaySession limitation: it excludes submodules by design,
    // but GlobalCatalog provides them for semantic indexing
    if (diag.code == "UnknownModule" && global_catalog != nullptr) {
      // Message format: "unknown module 'foo_module'"
      // Extract module name between single quotes
      std::string_view message = diag.message;
      auto start_pos = message.find('\'');
      auto end_pos = message.rfind('\'');

      if (start_pos != std::string_view::npos &&
          end_pos != std::string_view::npos && end_pos > start_pos) {
        auto module_name =
            message.substr(start_pos + 1, end_pos - start_pos - 1);

        // If GlobalCatalog has this module, it's a false positive
        // (module exists in project, just not in OverlaySession)
        if (global_catalog->GetModule(module_name) != nullptr) {
          continue;  // Skip this diagnostic
        }
      }
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

    // Downgrade UnresolvedHierarchicalPath to hint level (less intrusive)
    // This is an LSP limitation, not a code issue, so grey dotted hint is
    // appropriate
    if (toString(diag.code) == "UnresolvedHierarchicalPath") {
      lsp_diag.severity = lsp::DiagnosticSeverity::kHint;
    }

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

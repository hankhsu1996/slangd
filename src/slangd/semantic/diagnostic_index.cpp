#include "slangd/semantic/diagnostic_index.hpp"

#include <slang/diagnostics/DiagnosticEngine.h>
#include <slang/diagnostics/Diagnostics.h>

#include "slangd/utils/conversion.hpp"
#include "slangd/utils/path_utils.hpp"

namespace slangd::semantic {

auto DiagnosticIndex::FromCompilation(
    slang::ast::Compilation& compilation,
    const slang::SourceManager& source_manager, const std::string& uri,
    std::shared_ptr<spdlog::logger> logger) -> DiagnosticIndex {
  if (!logger) {
    logger = spdlog::default_logger();
  }

  logger->debug("Creating DiagnosticIndex for: {}", uri);

  // Extract diagnostics from compilation
  auto diagnostics = ExtractDiagnosticsFromCompilation(
      compilation, source_manager, uri, logger);

  // Apply filtering and modification
  auto filtered_diagnostics = FilterAndModifyDiagnostics(diagnostics, logger);

  logger->debug(
      "DiagnosticIndex created with {} diagnostics for: {}",
      filtered_diagnostics.size(), uri);

  return {std::move(filtered_diagnostics), uri, std::move(logger)};
}

DiagnosticIndex::DiagnosticIndex(
    std::vector<lsp::Diagnostic> diagnostics, std::string uri,
    std::shared_ptr<spdlog::logger> logger)
    : diagnostics_(std::move(diagnostics)),
      uri_(std::move(uri)),
      logger_(std::move(logger)) {
}

auto DiagnosticIndex::PrintInfo() const -> void {
  logger_->info(
      "DiagnosticIndex for {}: {} diagnostics", uri_, diagnostics_.size());
  for (const auto& diag : diagnostics_) {
    int severity_value = static_cast<int>(
        diag.severity.value_or(lsp::DiagnosticSeverity::kError));
    logger_->info(
        "  {} at {}:{}-{}:{}: {}", severity_value, diag.range.start.line,
        diag.range.start.character, diag.range.end.line,
        diag.range.end.character, diag.message);
  }
}

auto DiagnosticIndex::ExtractDiagnosticsFromCompilation(
    slang::ast::Compilation& compilation,
    const slang::SourceManager& source_manager, const std::string& uri,
    std::shared_ptr<spdlog::logger> logger) -> std::vector<lsp::Diagnostic> {
  std::vector<lsp::Diagnostic> diagnostics;

  // Create a diagnostic engine using the source manager
  // This ensures proper location information for diagnostics
  slang::DiagnosticEngine diagnostic_engine(source_manager);

  // Disable unnamed-generate warnings by default: start with "none"
  // then enable only the default group with "default"
  std::vector<std::string> warning_options = {"none", "default"};
  diagnostic_engine.setWarningOptions(warning_options);

  // Extract semantic diagnostics from compilation
  // This includes both syntax and semantic diagnostics
  auto slang_diagnostics = compilation.getAllDiagnostics();

  auto lsp_diagnostics = ConvertSlangDiagnosticsToLsp(
      slang_diagnostics, source_manager, diagnostic_engine, uri);

  logger->debug(
      "Extracted {} diagnostics from compilation for: {}",
      lsp_diagnostics.size(), uri);

  return lsp_diagnostics;
}

auto DiagnosticIndex::FilterAndModifyDiagnostics(
    std::vector<lsp::Diagnostic> diagnostics,
    std::shared_ptr<spdlog::logger> logger) -> std::vector<lsp::Diagnostic> {
  std::vector<lsp::Diagnostic> result;
  result.reserve(diagnostics.size());

  for (auto& diag : diagnostics) {
    // 1. Check for diagnostics to completely exclude
    if (diag.code == "InfoTask") {
      // Skip these diagnostics entirely
      continue;
    }

    // 2. Check for diagnostics to demote and enhance
    if (diag.code == "CouldNotOpenIncludeFile") {
      // Demote to warning
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

      // Add hint about configuration
      diag.message +=
          " (Consider configuring include directories in a .slangd file)";
    } else if (diag.code == "UnknownDirective") {
      // Demote to warning
      diag.severity = lsp::DiagnosticSeverity::kWarning;

      // Add hint about configuration
      diag.message += " (Add defines in .slangd file if needed)";
    }

    result.push_back(diag);
  }

  logger->debug(
      "DiagnosticIndex filtered {} diagnostics",
      diagnostics.size() - result.size());

  return result;
}

auto DiagnosticIndex::ConvertSlangDiagnosticsToLsp(
    const slang::Diagnostics& slang_diagnostics,
    const slang::SourceManager& source_manager,
    const slang::DiagnosticEngine& diag_engine, const std::string& uri)
    -> std::vector<lsp::Diagnostic> {
  std::vector<lsp::Diagnostic> result;

  for (const auto& diag : slang_diagnostics) {
    // Skip diagnostics not in our document
    if (!IsDiagnosticInUriDocument(diag, source_manager, uri)) {
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

// Helper functions - need to be static members to avoid linking issues
auto DiagnosticIndex::ConvertDiagnosticSeverityToLsp(
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

auto DiagnosticIndex::IsDiagnosticInUriDocument(
    const slang::Diagnostic& diag, const slang::SourceManager& source_manager,
    const std::string& uri) -> bool {
  if (!diag.location) {
    return false;
  }

  return IsLocationInDocument(diag.location, source_manager, uri);
}

}  // namespace slangd::semantic

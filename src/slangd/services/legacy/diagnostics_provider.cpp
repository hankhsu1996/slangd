#include "slangd/services/legacy/diagnostics_provider.hpp"

#include <string>
#include <vector>

#include <slang/ast/Compilation.h>
#include <slang/diagnostics/DiagnosticEngine.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/text/SourceLocation.h>
#include <slang/text/SourceManager.h>
#include <spdlog/spdlog.h>

#include "slangd/utils/conversion.hpp"
#include "slangd/utils/path_utils.hpp"

namespace slangd {

auto DiagnosticsProvider::GetDiagnosticsForUri(std::string uri)
    -> std::vector<lsp::Diagnostic> {
  // Get the compilation and syntax tree for this document
  auto compilation = document_manager_->GetCompilation(uri);
  auto syntax_tree = document_manager_->GetSyntaxTree(uri);
  auto source_manager = document_manager_->GetSourceManager(uri);

  // If any required component is missing, return empty vector
  if (!compilation || !syntax_tree || !source_manager) {
    return {};
  }

  auto diagnostics = ResolveDiagnosticsFromCompilation(
      compilation, syntax_tree, source_manager, uri);

  // Apply filtering and modification to diagnostics
  auto filtered_diagnostics =
      FilterAndModifyDiagnostics(std::move(diagnostics));

  logger_->debug(
      "DiagnosticsProvider found {} diagnostics in {}",
      filtered_diagnostics.size(), uri);

  return filtered_diagnostics;
}

auto DiagnosticsProvider::ResolveDiagnosticsFromCompilation(
    const std::shared_ptr<slang::ast::Compilation>& compilation,
    const std::shared_ptr<slang::syntax::SyntaxTree>& syntax_tree,
    const std::shared_ptr<slang::SourceManager>& source_manager,
    const std::string& uri) -> std::vector<lsp::Diagnostic> {
  std::vector<lsp::Diagnostic> diagnostics;

  // If any required component is missing, return empty vector
  if (!compilation || !syntax_tree || !source_manager) {
    return diagnostics;
  }

  // Create a diagnostic engine using the document's source manager
  // This ensures proper location information for diagnostics
  slang::DiagnosticEngine diagnostic_engine(*source_manager);

  // Disable unnamed-generate warnings by default: start with "none"
  // then enable only the default group with "default"
  std::vector<std::string> warning_options = {"none", "default"};
  diagnostic_engine.setWarningOptions(warning_options);

  // Extract semantic diagnostics if we have a compilation
  // This already includes syntax diagnostics
  if (compilation) {
    auto semantic_diags = ExtractSemanticDiagnostics(
        compilation, source_manager, diagnostic_engine, uri);
    diagnostics.insert(
        diagnostics.end(), semantic_diags.begin(), semantic_diags.end());
  }

  // Extract syntax diagnostics if we have a syntax tree
  else if (syntax_tree) {
    auto syntax_diags = ExtractSyntaxDiagnostics(
        syntax_tree, source_manager, diagnostic_engine, uri);
    diagnostics.insert(
        diagnostics.end(), syntax_diags.begin(), syntax_diags.end());
  }

  return diagnostics;
}

auto DiagnosticsProvider::ExtractSemanticDiagnostics(
    const std::shared_ptr<slang::ast::Compilation>& compilation,
    const std::shared_ptr<slang::SourceManager>& source_manager,
    const slang::DiagnosticEngine& diag_engine, const std::string& uri)
    -> std::vector<lsp::Diagnostic> {
  if (!compilation) {
    return {};
  }

  return ConvertDiagnosticsToLsp(
      compilation->getAllDiagnostics(), source_manager, diag_engine, uri);
}

auto DiagnosticsProvider::ExtractSyntaxDiagnostics(
    const std::shared_ptr<slang::syntax::SyntaxTree>& syntax_tree,
    const std::shared_ptr<slang::SourceManager>& source_manager,
    const slang::DiagnosticEngine& diag_engine, const std::string& uri)
    -> std::vector<lsp::Diagnostic> {
  if (!syntax_tree) {
    return {};
  }

  return ConvertDiagnosticsToLsp(
      syntax_tree->diagnostics(), source_manager, diag_engine, uri);
}

auto DiagnosticsProvider::ConvertDiagnosticsToLsp(
    const slang::Diagnostics& slang_diagnostics,
    const std::shared_ptr<slang::SourceManager>& source_manager,
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

auto DiagnosticsProvider::ConvertDiagnosticSeverityToLsp(
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

auto DiagnosticsProvider::IsDiagnosticInUriDocument(
    const slang::Diagnostic& diag,
    const std::shared_ptr<slang::SourceManager>& source_manager,
    const std::string& uri) -> bool {
  if (!diag.location) {
    return false;
  }

  return IsLocationInDocument(diag.location, source_manager, uri);
}

auto DiagnosticsProvider::FilterAndModifyDiagnostics(
    std::vector<lsp::Diagnostic> diagnostics) -> std::vector<lsp::Diagnostic> {
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

  logger_->debug(
      "DiagnosticsProvider filtered {} diagnostics",
      diagnostics.size() - result.size());

  return result;
}

}  // namespace slangd

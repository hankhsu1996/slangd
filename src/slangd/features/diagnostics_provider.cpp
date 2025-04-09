#include "slangd/features/diagnostics_provider.hpp"

#include <string>
#include <vector>

#include <slang/ast/Compilation.h>
#include <slang/diagnostics/DiagnosticEngine.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/text/SourceLocation.h>
#include <slang/text/SourceManager.h>
#include <spdlog/spdlog.h>

#include "slangd/utils/conversion.hpp"
#include "slangd/utils/source_utils.hpp"

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

  logger_->debug(
      "DiagnosticsProvider found {} diagnostics in {}", diagnostics.size(),
      uri);

  return diagnostics;
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
      lsp_diag.range =
          ConvertSlangRangesToLspRange(diag.ranges, source_manager);
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

}  // namespace slangd

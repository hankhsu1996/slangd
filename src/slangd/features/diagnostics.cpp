#include "slangd/features/diagnostics.hpp"

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

namespace {

// Map Slang diagnostic severity to LSP diagnostic severity
lsp::DiagnosticSeverity ConvertSeverity(slang::DiagnosticSeverity severity) {
  switch (severity) {
    case slang::DiagnosticSeverity::Ignored:
      return lsp::DiagnosticSeverity::Hint;
    case slang::DiagnosticSeverity::Note:
      return lsp::DiagnosticSeverity::Information;
    case slang::DiagnosticSeverity::Warning:
      return lsp::DiagnosticSeverity::Warning;
    case slang::DiagnosticSeverity::Error:
    case slang::DiagnosticSeverity::Fatal:
      return lsp::DiagnosticSeverity::Error;
    default:
      return lsp::DiagnosticSeverity::Information;
  }
}

// Checks if a diagnostic belongs to the specified document URI
bool IsDiagnosticInDocument(
    const slang::Diagnostic& diag,
    const std::shared_ptr<slang::SourceManager>& source_manager,
    const std::string& uri) {
  if (!diag.location) {
    return false;
  }

  return IsLocationInDocument(diag.location, source_manager, uri);
}

}  // namespace

std::vector<lsp::Diagnostic> ExtractSyntaxDiagnostics(
    const std::shared_ptr<slang::syntax::SyntaxTree>& syntax_tree,
    const std::shared_ptr<slang::SourceManager>& source_manager,
    const slang::DiagnosticEngine& diag_engine, const std::string& uri) {
  if (!syntax_tree) {
    return {};
  }

  return ConvertDiagnostics(
      syntax_tree->diagnostics(), source_manager, diag_engine, uri);
}

std::vector<lsp::Diagnostic> ExtractSemanticDiagnostics(
    const std::shared_ptr<slang::ast::Compilation>& compilation,
    const std::shared_ptr<slang::SourceManager>& source_manager,
    const slang::DiagnosticEngine& diag_engine, const std::string& uri) {
  if (!compilation) {
    return {};
  }

  return ConvertDiagnostics(
      compilation->getAllDiagnostics(), source_manager, diag_engine, uri);
}

std::vector<lsp::Diagnostic> GetDocumentDiagnostics(
    const std::shared_ptr<slang::syntax::SyntaxTree>& syntax_tree,
    const std::shared_ptr<slang::ast::Compilation>& compilation,
    const std::shared_ptr<slang::SourceManager>& source_manager,
    const slang::DiagnosticEngine& diag_engine, const std::string& uri) {
  std::vector<lsp::Diagnostic> diagnostics;

  // Extract semantic diagnostics if we have a compilation
  // This already includes syntax diagnostics
  if (compilation) {
    auto semantic_diags = ExtractSemanticDiagnostics(
        compilation, source_manager, diag_engine, uri);
    diagnostics.insert(
        diagnostics.end(), semantic_diags.begin(), semantic_diags.end());
  }

  // Extract syntax diagnostics if we have a syntax tree
  else if (syntax_tree) {
    auto syntax_diags =
        ExtractSyntaxDiagnostics(syntax_tree, source_manager, diag_engine, uri);
    diagnostics.insert(
        diagnostics.end(), syntax_diags.begin(), syntax_diags.end());
  }

  spdlog::info(
      "GetDocumentDiagnostics extracted {} diagnostics for {}",
      diagnostics.size(), uri);

  return diagnostics;
}

std::vector<lsp::Diagnostic> ConvertDiagnostics(
    const slang::Diagnostics& slang_diagnostics,
    const std::shared_ptr<slang::SourceManager>& source_manager,
    const slang::DiagnosticEngine& diag_engine, const std::string& uri) {
  std::vector<lsp::Diagnostic> result;

  for (const auto& diag : slang_diagnostics) {
    // Skip diagnostics not in our document
    if (!IsDiagnosticInDocument(diag, source_manager, uri)) {
      continue;
    }

    // Create the LSP diagnostic
    lsp::Diagnostic lsp_diag;

    // Get severity from the diagnostic engine
    lsp_diag.severity =
        ConvertSeverity(diag_engine.getSeverity(diag.code, diag.location));

    // Format message using the diagnostic engine
    lsp_diag.message = diag_engine.formatMessage(diag);

    // Convert location to range
    if (diag.ranges.size() > 0) {
      lsp_diag.range =
          ConvertSlangRangesToLspRange(diag.ranges, source_manager);
    } else {
      // Fallback to an empty range at the start of the file
      lsp_diag.range = lsp::Range{lsp::Position{0, 0}, lsp::Position{0, 0}};
    }

    // Add optional code and source fields
    lsp_diag.code = toString(diag.code);
    lsp_diag.source = "slang";

    result.push_back(lsp_diag);
  }

  return result;
}

}  // namespace slangd

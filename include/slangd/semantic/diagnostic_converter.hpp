#pragma once

#include <memory>
#include <string>
#include <vector>

#include <lsp/basic.hpp>
#include <lsp/document_features.hpp>
#include <slang/ast/Compilation.h>
#include <slang/diagnostics/DiagnosticEngine.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/text/SourceManager.h>
#include <spdlog/spdlog.h>

namespace slangd::semantic {

// Stateless utility for converting Slang diagnostics to LSP format
// Provides both parse-only (fast) and full (semantic) diagnostic extraction
class DiagnosticConverter {
 public:
  DiagnosticConverter() = delete;

  // Extract parse-only diagnostics (syntax errors, no elaboration)
  // Does not trigger semantic analysis
  static auto ExtractParseDiagnostics(
      slang::ast::Compilation& compilation,
      const slang::SourceManager& source_manager, const std::string& uri,
      std::shared_ptr<spdlog::logger> logger = nullptr)
      -> std::vector<lsp::Diagnostic>;

  // Extract all diagnostics (syntax + semantic errors)
  // Triggers full semantic analysis and elaboration
  static auto ExtractAllDiagnostics(
      slang::ast::Compilation& compilation,
      const slang::SourceManager& source_manager, const std::string& uri,
      std::shared_ptr<spdlog::logger> logger = nullptr)
      -> std::vector<lsp::Diagnostic>;

 private:
  // Core extraction logic - converts Slang diagnostics to LSP format
  static auto ExtractDiagnostics(
      const slang::Diagnostics& slang_diagnostics,
      const slang::SourceManager& source_manager, const std::string& uri)
      -> std::vector<lsp::Diagnostic>;

  // Filter and modify diagnostics for LSP consumption
  static auto FilterAndModifyDiagnostics(
      std::vector<lsp::Diagnostic> diagnostics) -> std::vector<lsp::Diagnostic>;

  // Convert Slang diagnostics to LSP format
  static auto ConvertSlangDiagnosticsToLsp(
      const slang::Diagnostics& slang_diagnostics,
      const slang::SourceManager& source_manager,
      const slang::DiagnosticEngine& diag_engine, const std::string& uri)
      -> std::vector<lsp::Diagnostic>;

  // Helper conversion functions
  static auto ConvertDiagnosticSeverityToLsp(slang::DiagnosticSeverity severity)
      -> lsp::DiagnosticSeverity;

  static auto IsDiagnosticInUriDocument(
      const slang::Diagnostic& diag, const slang::SourceManager& source_manager,
      const std::string& uri) -> bool;
};

}  // namespace slangd::semantic

#pragma once

#include <memory>
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

  // Extract syntax errors only (fast, no semantic analysis)
  static auto ExtractParseDiagnostics(
      slang::ast::Compilation& compilation,
      const slang::SourceManager& source_manager,
      slang::BufferID main_buffer_id,
      std::shared_ptr<spdlog::logger> logger = nullptr)
      -> std::vector<lsp::Diagnostic>;

  // Extract diagnostics collected during file-scoped traversal (NO elaboration)
  // This only returns diagnostics that have been added to diagMap during
  // limited AST traversal, WITHOUT triggering full design elaboration
  static auto ExtractCollectedDiagnostics(
      slang::ast::Compilation& compilation,
      const slang::SourceManager& source_manager,
      slang::BufferID main_buffer_id) -> std::vector<lsp::Diagnostic>;

  // Extract diagnostics from pre-computed slang::Diagnostics
  // (used for two-phase diagnostic publishing)
  static auto ExtractDiagnostics(
      const slang::Diagnostics& slang_diagnostics,
      const slang::SourceManager& source_manager,
      slang::BufferID main_buffer_id) -> std::vector<lsp::Diagnostic>;

  // Apply LSP-specific filtering
  static auto FilterDiagnostics(std::vector<lsp::Diagnostic> diagnostics)
      -> std::vector<lsp::Diagnostic>;

 private:
  static auto ConvertSlangDiagnosticsToLsp(
      const slang::Diagnostics& slang_diagnostics,
      const slang::SourceManager& source_manager,
      const slang::DiagnosticEngine& diag_engine,
      slang::BufferID main_buffer_id) -> std::vector<lsp::Diagnostic>;

  static auto ConvertDiagnosticSeverityToLsp(slang::DiagnosticSeverity severity)
      -> lsp::DiagnosticSeverity;
};

}  // namespace slangd::semantic

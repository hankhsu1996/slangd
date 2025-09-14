#pragma once

#include <memory>
#include <string>
#include <vector>

#include <asio.hpp>
#include <slang/ast/Compilation.h>
#include <slang/diagnostics/DiagnosticEngine.h>
#include <slang/diagnostics/Diagnostics.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/text/SourceManager.h>

#include "slangd/services/legacy/document_manager.hpp"
#include "slangd/services/legacy/workspace_manager.hpp"
#include "slangd/services/legacy/language_feature_provider.hpp"

namespace slangd {

class DiagnosticsProvider : public LanguageFeatureProvider {
 public:
  DiagnosticsProvider(
      std::shared_ptr<DocumentManager> document_manager,
      std::shared_ptr<WorkspaceManager> workspace_manager,
      std::shared_ptr<spdlog::logger> logger = nullptr)
      : LanguageFeatureProvider(document_manager, workspace_manager, logger) {
  }

  // Top-level API to get diagnostics for a document
  auto GetDiagnosticsForUri(std::string uri) -> std::vector<lsp::Diagnostic>;

  // Core orchestration: gather all relevant diagnostics
  static auto ResolveDiagnosticsFromCompilation(
      const std::shared_ptr<slang::ast::Compilation>& compilation,
      const std::shared_ptr<slang::syntax::SyntaxTree>& syntax_tree,
      const std::shared_ptr<slang::SourceManager>& source_manager,
      const std::string& uri) -> std::vector<lsp::Diagnostic>;

  // Filter and modify diagnostics before returning to client
  // - Exclude certain diagnostics
  // - Demote severity of specific diagnostics
  // - Enhance messages with .slangd configuration hints
  auto FilterAndModifyDiagnostics(std::vector<lsp::Diagnostic> diagnostics)
      -> std::vector<lsp::Diagnostic>;

 private:
  // Semantic and syntax diagnostic extraction
  static auto ExtractSemanticDiagnostics(
      const std::shared_ptr<slang::ast::Compilation>& compilation,
      const std::shared_ptr<slang::SourceManager>& source_manager,
      const slang::DiagnosticEngine& diag_engine, const std::string& uri)
      -> std::vector<lsp::Diagnostic>;

  static auto ExtractSyntaxDiagnostics(
      const std::shared_ptr<slang::syntax::SyntaxTree>& syntax_tree,
      const std::shared_ptr<slang::SourceManager>& source_manager,
      const slang::DiagnosticEngine& diag_engine, const std::string& uri)
      -> std::vector<lsp::Diagnostic>;

  // Conversion utilities (Slang → LSP)
  static auto ConvertDiagnosticsToLsp(
      const slang::Diagnostics& slang_diagnostics,
      const std::shared_ptr<slang::SourceManager>& source_manager,
      const slang::DiagnosticEngine& diag_engine, const std::string& uri)
      -> std::vector<lsp::Diagnostic>;

  static auto ConvertDiagnosticSeverityToLsp(slang::DiagnosticSeverity severity)
      -> lsp::DiagnosticSeverity;

  // Location-based filtering
  static auto IsDiagnosticInUriDocument(
      const slang::Diagnostic& diag,
      const std::shared_ptr<slang::SourceManager>& source_manager,
      const std::string& uri) -> bool;
};

}  // namespace slangd

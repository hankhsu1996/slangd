#pragma once

#include <memory>
#include <string>
#include <vector>

#include <slang/ast/Compilation.h>
#include <slang/diagnostics/DiagnosticEngine.h>
#include <slang/diagnostics/Diagnostics.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/text/SourceManager.h>

#include "slangd/core/document_manager.hpp"
#include "slangd/core/workspace_manager.hpp"
#include "slangd/features/language_feature_provider.hpp"

namespace slangd {

class DiagnosticsProvider : public LanguageFeatureProvider {
 public:
  DiagnosticsProvider(
      std::shared_ptr<DocumentManager> document_manager,
      std::shared_ptr<WorkspaceManager> workspace_manager,
      std::shared_ptr<spdlog::logger> logger = nullptr)
      : LanguageFeatureProvider(document_manager, workspace_manager),
        logger_(logger ? logger : spdlog::default_logger()) {
  }

  auto Logger() -> std::shared_ptr<spdlog::logger> {
    return logger_;
  }

  // Get all diagnostics for a document
  auto GetDocumentDiagnostics(std::string uri) -> std::vector<lsp::Diagnostic>;

  // Collect diagnostics from a compilation, syntax tree, and source manager
  // and return them as LSP diagnostics
  static auto CollectDiagnostics(
      const std::shared_ptr<slang::ast::Compilation>& compilation,
      const std::shared_ptr<slang::syntax::SyntaxTree>& syntax_tree,
      const std::shared_ptr<slang::SourceManager>& source_manager,
      const std::string& uri) -> std::vector<lsp::Diagnostic>;

  // Convert severity to LSP severity
  static auto ConvertSeverity(slang::DiagnosticSeverity severity)
      -> lsp::DiagnosticSeverity;

  // Check if a diagnostic belongs to the specified document URI
  static auto IsDiagnosticInDocument(
      const slang::Diagnostic& diag,
      const std::shared_ptr<slang::SourceManager>& source_manager,
      const std::string& uri) -> bool;

  // Extract syntax diagnostics from a syntax tree
  static auto ExtractSyntaxDiagnostics(
      const std::shared_ptr<slang::syntax::SyntaxTree>& syntax_tree,
      const std::shared_ptr<slang::SourceManager>& source_manager,
      const slang::DiagnosticEngine& diag_engine, const std::string& uri)
      -> std::vector<lsp::Diagnostic>;

  // Extract semantic diagnostics from a compilation
  static auto ExtractSemanticDiagnostics(
      const std::shared_ptr<slang::ast::Compilation>& compilation,
      const std::shared_ptr<slang::SourceManager>& source_manager,
      const slang::DiagnosticEngine& diag_engine, const std::string& uri)
      -> std::vector<lsp::Diagnostic>;

  // Convert Slang diagnostics to LSP diagnostics
  static auto ConvertDiagnostics(
      const slang::Diagnostics& slang_diagnostics,
      const std::shared_ptr<slang::SourceManager>& source_manager,
      const slang::DiagnosticEngine& diag_engine, const std::string& uri)
      -> std::vector<lsp::Diagnostic>;

 private:
  std::shared_ptr<spdlog::logger> logger_;
};

}  // namespace slangd

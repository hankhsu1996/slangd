#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <asio.hpp>
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
      asio::any_io_executor executor,
      std::shared_ptr<DocumentManager> document_manager,
      std::shared_ptr<WorkspaceManager> workspace_manager,
      std::shared_ptr<spdlog::logger> logger = nullptr)
      : LanguageFeatureProvider(document_manager, workspace_manager, logger),
        strand_(asio::make_strand(executor)) {
  }

  // Schedule diagnostics with debouncing
  // The publisher callback will be called after the debounce period
  auto ScheduleDiagnostics(
      std::string uri, std::string text, int version,
      std::function<
          asio::awaitable<void>(std::string, std::vector<lsp::Diagnostic>, int)>
          publisher) -> void;

  // Force immediate diagnostics (e.g., on document save)
  auto ProcessImmediateDiagnostics(
      std::string uri, std::string text, int version,
      std::function<
          asio::awaitable<void>(std::string, std::vector<lsp::Diagnostic>, int)>
          publisher) -> asio::awaitable<void>;

  // Top-level API to get diagnostics for a document
  auto GetDiagnosticsForUri(std::string uri) -> std::vector<lsp::Diagnostic>;

  // Core orchestration: gather all relevant diagnostics
  static auto ResolveDiagnosticsFromCompilation(
      const std::shared_ptr<slang::ast::Compilation>& compilation,
      const std::shared_ptr<slang::syntax::SyntaxTree>& syntax_tree,
      const std::shared_ptr<slang::SourceManager>& source_manager,
      const std::string& uri) -> std::vector<lsp::Diagnostic>;

 private:
  // Debounce management
  struct PendingRequest {
    std::string text;
    int version;
    std::unique_ptr<asio::steady_timer> timer;
    std::function<asio::awaitable<void>(
        std::string, std::vector<lsp::Diagnostic>, int)>
        publisher;
  };

  // Process diagnostics after debounce
  auto ProcessDiagnostics(std::string uri) -> asio::awaitable<void>;

  // Filter and modify diagnostics before returning to client
  // - Exclude certain diagnostics
  // - Demote severity of specific diagnostics
  // - Enhance messages with .slangd configuration hints
  auto FilterAndModifyDiagnostics(std::vector<lsp::Diagnostic> diagnostics)
      -> std::vector<lsp::Diagnostic>;

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

  // Debounce tracking
  std::unordered_map<std::string, PendingRequest> pending_requests_;
  asio::strand<asio::any_io_executor> strand_;
  std::chrono::milliseconds debounce_delay_{500};
};

}  // namespace slangd

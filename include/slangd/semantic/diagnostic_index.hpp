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

// Index of diagnostics extracted from a Slang compilation
// Mirrors DefinitionIndex pattern for consistent architecture
class DiagnosticIndex {
 public:
  DiagnosticIndex() = delete;

  // Create a diagnostic index from a compilation
  // Extracts and filters diagnostics for LSP consumption
  static auto FromCompilation(
      slang::ast::Compilation& compilation,
      const slang::SourceManager& source_manager, const std::string& uri,
      std::shared_ptr<spdlog::logger> logger = nullptr) -> DiagnosticIndex;

  // Access the extracted diagnostics
  [[nodiscard]] auto GetDiagnostics() const
      -> const std::vector<lsp::Diagnostic>& {
    return diagnostics_;
  }

  // Get the URI this index was created for
  [[nodiscard]] auto GetUri() const -> const std::string& {
    return uri_;
  }

  // Helper method for debugging
  auto PrintInfo() const -> void;

 private:
  // Private constructor - use FromCompilation() factory method
  DiagnosticIndex(
      std::vector<lsp::Diagnostic> diagnostics, std::string uri,
      std::shared_ptr<spdlog::logger> logger);

  // Core extraction logic - semantic and syntax diagnostics
  static auto ExtractDiagnosticsFromCompilation(
      slang::ast::Compilation& compilation,
      const slang::SourceManager& source_manager, const std::string& uri,
      std::shared_ptr<spdlog::logger> logger) -> std::vector<lsp::Diagnostic>;

  // Filter and modify diagnostics for LSP consumption
  static auto FilterAndModifyDiagnostics(
      std::vector<lsp::Diagnostic> diagnostics,
      std::shared_ptr<spdlog::logger> logger) -> std::vector<lsp::Diagnostic>;

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

  // Core data
  std::vector<lsp::Diagnostic> diagnostics_;
  std::string uri_;
  std::shared_ptr<spdlog::logger> logger_;
};

}  // namespace slangd::semantic

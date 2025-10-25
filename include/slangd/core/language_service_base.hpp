#pragma once

#include <string>
#include <vector>

#include <asio.hpp>
#include <lsp/basic.hpp>
#include <lsp/document_features.hpp>
#include <lsp/error.hpp>
#include <lsp/workspace.hpp>

namespace slangd {

using lsp::error::LspError;

// High-level business operations base class for LSP domain logic
// Replaces individual providers with unified interface
// Enables different service implementations (Legacy, GlobalIndex, Hybrid)
class LanguageServiceBase {
 public:
  LanguageServiceBase() = default;
  LanguageServiceBase(const LanguageServiceBase&) = default;
  LanguageServiceBase(LanguageServiceBase&&) = delete;
  auto operator=(const LanguageServiceBase&) -> LanguageServiceBase& = default;
  auto operator=(LanguageServiceBase&&) -> LanguageServiceBase& = delete;
  virtual ~LanguageServiceBase() = default;

  // Diagnostic publishing is fundamental to all LSP implementations
  using DiagnosticPublisher = std::function<void(
      std::string uri, int version, std::vector<lsp::Diagnostic>)>;

  virtual auto SetDiagnosticPublisher(DiagnosticPublisher publisher)
      -> void = 0;

  // Status publishing for language server state (idle, indexing, etc.)
  using StatusPublisher = std::function<void(std::string status)>;

  virtual auto SetStatusPublisher(StatusPublisher publisher) -> void = 0;

  // Diagnostics computation - async operations
  // Compute diagnostics from parsing only (syntax errors)
  virtual auto ComputeParseDiagnostics(std::string uri, std::string content)
      -> asio::awaitable<
          std::expected<std::vector<lsp::Diagnostic>, LspError>> = 0;

  // Find definitions at the given position
  virtual auto GetDefinitionsForPosition(
      std::string uri, lsp::Position position)
      -> asio::awaitable<
          std::expected<std::vector<lsp::Location>, LspError>> = 0;

  // Get document symbol hierarchy
  virtual auto GetDocumentSymbols(std::string uri) -> asio::awaitable<
      std::expected<std::vector<lsp::DocumentSymbol>, LspError>> = 0;

  // Workspace initialization - called during LSP initialize
  virtual auto InitializeWorkspace(std::string workspace_uri)
      -> asio::awaitable<void> = 0;

  // Config change handling - notifies service of configuration file changes
  virtual auto HandleConfigChange() -> asio::awaitable<void> = 0;

  // Source file change handling - notifies service of source file changes
  virtual auto HandleSourceFileChange(
      std::string uri, lsp::FileChangeType change_type)
      -> asio::awaitable<void> = 0;

  // Document lifecycle events (protocol-level)
  // Called when document is opened in editor
  virtual auto OnDocumentOpened(
      std::string uri, std::string content, int version)
      -> asio::awaitable<void> = 0;

  // Called when document content changes (typing/editing)
  virtual auto OnDocumentChanged(
      std::string uri, std::string content, int version)
      -> asio::awaitable<void> = 0;

  // Called when document is saved
  virtual auto OnDocumentSaved(std::string uri) -> asio::awaitable<void> = 0;

  // Called when document is closed in editor
  virtual auto OnDocumentClosed(std::string uri) -> void = 0;

  // Called when external file changes are detected
  virtual auto OnDocumentsChanged(std::vector<std::string> uris) -> void = 0;

  // Check if document is currently open in editor (synchronous)
  virtual auto IsDocumentOpen(const std::string& uri) const -> bool = 0;
};

}  // namespace slangd

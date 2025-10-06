#pragma once

#include <string>
#include <vector>

#include <asio.hpp>
#include <lsp/basic.hpp>
#include <lsp/document_features.hpp>
#include <lsp/workspace.hpp>

namespace slangd {

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

  // Diagnostics computation - async because may need parsing/compilation
  // Parse diagnostics only - syntax errors without elaboration
  virtual auto ComputeParseDiagnostics(std::string uri, std::string content)
      -> asio::awaitable<std::vector<lsp::Diagnostic>> = 0;

  // Full diagnostics including semantic analysis
  virtual auto ComputeDiagnostics(std::string uri)
      -> asio::awaitable<std::vector<lsp::Diagnostic>> = 0;

  // Definition lookup - async because may need overlay creation
  virtual auto GetDefinitionsForPosition(
      std::string uri, lsp::Position position, std::string content)
      -> asio::awaitable<std::vector<lsp::Location>> = 0;

  // Document symbols - async because may need overlay creation
  virtual auto GetDocumentSymbols(std::string uri, std::string content)
      -> asio::awaitable<std::vector<lsp::DocumentSymbol>> = 0;

  // Workspace initialization - called during LSP initialize
  virtual auto InitializeWorkspace(std::string workspace_uri)
      -> asio::awaitable<void> = 0;

  // Config change handling - notifies service of configuration file changes
  virtual auto HandleConfigChange() -> void = 0;

  // Source file change handling - notifies service of source file changes
  virtual auto HandleSourceFileChange(
      std::string uri, lsp::FileChangeType change_type) -> void = 0;

  // Session lifecycle management
  // Update/create session for document (called on save/open)
  virtual auto UpdateSession(std::string uri, std::string content)
      -> asio::awaitable<void> = 0;

  // Remove session for closed document
  virtual auto RemoveSession(std::string uri) -> void = 0;

  // Invalidate sessions for external file changes
  virtual auto InvalidateSessions(std::vector<std::string> uris) -> void = 0;
};

}  // namespace slangd

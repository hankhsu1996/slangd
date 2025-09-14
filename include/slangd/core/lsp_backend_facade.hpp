#pragma once

#include <string>
#include <vector>

#include <asio.hpp>
#include <lsp/basic.hpp>
#include <lsp/document_features.hpp>

namespace slangd {

// High-level business operations facade for LSP domain logic
// Replaces individual providers with unified interface
// Enables different backend implementations (Legacy, GlobalIndex, Hybrid)
class LspBackendFacade {
 public:
  LspBackendFacade() = default;
  LspBackendFacade(const LspBackendFacade &) = default;
  LspBackendFacade(LspBackendFacade &&) = delete;
  auto operator=(const LspBackendFacade &) -> LspBackendFacade & = default;
  auto operator=(LspBackendFacade &&) -> LspBackendFacade & = delete;
  virtual ~LspBackendFacade() = default;

  // Diagnostics computation - async because may need parsing/compilation
  virtual auto ComputeDiagnostics(std::string uri, std::string content)
      -> asio::awaitable<std::vector<lsp::Diagnostic>> = 0;

  // Definition lookup - sync because operates on existing compiled data
  virtual auto GetDefinitionsForPosition(
      std::string uri, lsp::Position position)
      -> std::vector<lsp::Location> = 0;

  // Document symbols - sync because operates on existing compiled data
  virtual auto GetDocumentSymbols(std::string uri)
      -> std::vector<lsp::DocumentSymbol> = 0;
};

}  // namespace slangd

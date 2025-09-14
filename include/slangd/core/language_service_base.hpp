#pragma once

#include <string>
#include <vector>

#include <asio.hpp>
#include <lsp/basic.hpp>
#include <lsp/document_features.hpp>

namespace slangd {

// High-level business operations base class for LSP domain logic
// Replaces individual providers with unified interface
// Enables different service implementations (Legacy, GlobalIndex, Hybrid)
class LanguageServiceBase {
 public:
  LanguageServiceBase() = default;
  LanguageServiceBase(const LanguageServiceBase &) = default;
  LanguageServiceBase(LanguageServiceBase &&) = delete;
  auto operator=(const LanguageServiceBase &) -> LanguageServiceBase & = default;
  auto operator=(LanguageServiceBase &&) -> LanguageServiceBase & = delete;
  virtual ~LanguageServiceBase() = default;

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

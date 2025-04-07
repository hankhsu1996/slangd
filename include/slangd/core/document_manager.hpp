#pragma once

#include <expected>
#include <memory>
#include <string>
#include <unordered_map>

#include <asio.hpp>
#include <slang/ast/Compilation.h>
#include <slang/ast/Symbol.h>
#include <slang/diagnostics/DiagnosticEngine.h>
#include <slang/syntax/SyntaxTree.h>
#include <spdlog/spdlog.h>

#include "lsp/basic.hpp"
#include "lsp/document_features.hpp"

namespace slangd {

class DocumentManager {
 public:
  explicit DocumentManager(
      asio::any_io_executor executor,
      std::shared_ptr<spdlog::logger> logger = nullptr);

  auto Logger() -> std::shared_ptr<spdlog::logger> {
    return logger_;
  }

  // Parse a document with compilation (fast)
  auto ParseWithCompilation(std::string uri, std::string content)
      -> asio::awaitable<void>;

  // Parse a document with full elaboration (slow)
  auto ParseWithElaboration(std::string uri, std::string content)
      -> asio::awaitable<void>;

  // Get the syntax tree for a document
  auto GetSyntaxTree(std::string uri)
      -> asio::awaitable<std::shared_ptr<slang::syntax::SyntaxTree>>;

  // Get the compilation for a document
  auto GetCompilation(std::string uri)
      -> asio::awaitable<std::shared_ptr<slang::ast::Compilation>>;

  // Get the symbols for a document
  auto GetSymbols(std::string uri) -> asio::awaitable<
      std::vector<std::shared_ptr<const slang::ast::Symbol>>>;

  // Get hierarchical document symbols defined in a document
  auto GetDocumentSymbols(std::string uri)
      -> asio::awaitable<std::vector<lsp::DocumentSymbol>>;

  // Get diagnostics for a document
  auto GetDocumentDiagnostics(std::string uri)
      -> asio::awaitable<std::vector<lsp::Diagnostic>>;

  // Find a symbol at a given position
  auto FindSymbolAtPosition(std::string uri, lsp::Position position)
      -> asio::awaitable<std::shared_ptr<const slang::ast::Symbol>>;

 private:
  // Logger
  std::shared_ptr<spdlog::logger> logger_;

  // ASIO executor reference
  asio::any_io_executor executor_;

  // Strand for synchronizing access to shared data
  asio::strand<asio::any_io_executor> strand_;

  // Maps document URIs to their syntax trees
  std::unordered_map<std::string, std::shared_ptr<slang::syntax::SyntaxTree>>
      syntax_trees_;

  // Maps document URIs to their compilation objects
  std::unordered_map<std::string, std::shared_ptr<slang::ast::Compilation>>
      compilations_;

  // Maps document URIs to their source managers
  std::unordered_map<std::string, std::shared_ptr<slang::SourceManager>>
      source_managers_;
};

}  // namespace slangd

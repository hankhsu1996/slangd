#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include <asio.hpp>
#include <slang/ast/Compilation.h>
#include <slang/ast/Symbol.h>
#include <slang/syntax/SyntaxTree.h>
#include <spdlog/spdlog.h>

#include "lsp/document_features.hpp"
#include "slangd/semantic/symbol_index.hpp"

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
      -> std::shared_ptr<slang::syntax::SyntaxTree>;

  // Get the compilation for a document
  auto GetCompilation(std::string uri)
      -> std::shared_ptr<slang::ast::Compilation>;

  // Get the source manager for a document
  auto GetSourceManager(std::string uri)
      -> std::shared_ptr<slang::SourceManager>;

  // Get the symbol index for a document
  auto GetSymbolIndex(std::string uri)
      -> std::shared_ptr<semantic::SymbolIndex>;

  // Get the symbols for a document
  auto GetSymbols(std::string uri)
      -> std::vector<std::shared_ptr<const slang::ast::Symbol>>;

  // Get hierarchical document symbols defined in a document
  auto GetDocumentSymbols(std::string uri) -> std::vector<lsp::DocumentSymbol>;

 private:
  // Executor
  asio::any_io_executor executor_;

  // Logger
  std::shared_ptr<spdlog::logger> logger_;

  // Maps document URIs to their syntax trees
  std::unordered_map<std::string, std::shared_ptr<slang::syntax::SyntaxTree>>
      syntax_trees_;

  // Maps document URIs to their compilation objects
  std::unordered_map<std::string, std::shared_ptr<slang::ast::Compilation>>
      compilations_;

  // Maps document URIs to their source managers
  std::unordered_map<std::string, std::shared_ptr<slang::SourceManager>>
      source_managers_;

  // Maps document URIs to their symbol indices
  std::unordered_map<std::string, std::shared_ptr<semantic::SymbolIndex>>
      symbol_indices_;
};

}  // namespace slangd

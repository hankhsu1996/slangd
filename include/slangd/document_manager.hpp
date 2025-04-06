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

/**
 * @brief Possible errors that can occur during parsing
 */
enum class ParseError {
  kSyntaxError,
  kFileNotFound,
  kEncodingError,
  kCompilationError,
  kElaborationError,
  kSlangInternalError,
  kUnknownError
};

/**
 * @brief Manages documents and their syntax trees
 *
 * This class is responsible for parsing SystemVerilog documents
 * and maintaining their syntax trees and compilation objects.
 */
class DocumentManager {
 public:
  /**
   * @brief Construct a new Document Manager object
   *
   * @param executor ASIO executor for async operations
   */
  explicit DocumentManager(
      asio::any_io_executor executor,
      std::shared_ptr<spdlog::logger> logger = nullptr);

  auto Logger() -> std::shared_ptr<spdlog::logger> {
    return logger_;
  }

  /**
   * @brief Parse a document with compilation
   *
   * This method creates or updates a syntax tree and performs compilation
   * to find both syntax and semantic errors. Fast enough for interactive use.
   *
   * @param uri The document URI
   * @param content The document content
   * @return asio::awaitable<std::expected<void, ParseError>> Result of the
   * parsing operation
   */
  auto ParseWithCompilation(std::string uri, std::string content)
      -> asio::awaitable<std::expected<void, ParseError>>;

  /**
   * @brief Parse a document with full elaboration (slow)
   *
   * This method creates or updates a syntax tree, performs compilation,
   * and runs full elaboration for complete semantic analysis.
   *
   * @param uri The document URI
   * @param content The document content
   * @return asio::awaitable<std::expected<void, ParseError>> Result of the
   * parsing operation
   */
  auto ParseWithElaboration(std::string uri, std::string content)
      -> asio::awaitable<std::expected<void, ParseError>>;

  /**
   * @brief Get the syntax tree for a document
   *
   * @param uri The document URI
   * @return asio::awaitable<std::shared_ptr<slang::syntax::SyntaxTree>> The
   * syntax tree or nullptr if not found
   */
  auto GetSyntaxTree(std::string uri)
      -> asio::awaitable<std::shared_ptr<slang::syntax::SyntaxTree>>;

  /**
   * @brief Get the compilation for a document
   *
   * @param uri The document URI
   * @return asio::awaitable<std::shared_ptr<slang::ast::Compilation>> The
   * compilation or nullptr if not found
   */
  auto GetCompilation(std::string uri)
      -> asio::awaitable<std::shared_ptr<slang::ast::Compilation>>;

  /**
   * @brief Get a list of symbols defined in a document
   *
   * @param uri The document URI
   * @return asio::awaitable<std::vector<std::shared_ptr<const
   * slang::ast::Symbol>>> List of symbols or empty vector if not found
   */
  auto GetSymbols(std::string uri) -> asio::awaitable<
      std::vector<std::shared_ptr<const slang::ast::Symbol>>>;

  /**
   * @brief Get hierarchical document symbols defined in a document
   *
   * @param uri The document URI
   * @return asio::awaitable<std::vector<lsp::DocumentSymbol>> Hierarchical
   * document symbols or empty vector if not found
   */
  auto GetDocumentSymbols(std::string uri)
      -> asio::awaitable<std::vector<lsp::DocumentSymbol>>;

  /**
   * @brief Get diagnostics for a document
   *
   * @param uri The document URI
   * @return asio::awaitable<std::vector<lsp::Diagnostic>> Diagnostics for the
   * document
   */
  auto GetDocumentDiagnostics(std::string uri)
      -> asio::awaitable<std::vector<lsp::Diagnostic>>;

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

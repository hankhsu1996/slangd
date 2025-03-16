#pragma once

#include <expected>
#include <memory>
#include <slang/ast/Compilation.h>
#include <slang/ast/Symbol.h>
#include <slang/syntax/SyntaxTree.h>
#include <string>
#include <unordered_map>

#include <asio.hpp>

namespace slangd {

/**
 * @brief Possible errors that can occur during parsing
 */
enum class ParseError {
  SyntaxError,
  FileNotFound,
  EncodingError,
  SlangInternalError,
  UnknownError
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
   * @param io_context ASIO io_context for async operations
   */
  DocumentManager(asio::io_context& io_context);

  /**
   * @brief Parse a document and create a syntax tree
   *
   * @param uri The document URI
   * @param content The document content
   * @return asio::awaitable<std::expected<void, ParseError>> Result of the
   * parsing operation
   */
  asio::awaitable<std::expected<void, ParseError>> ParseDocument(
      const std::string& uri, const std::string& content);

  /**
   * @brief Get the syntax tree for a document
   *
   * @param uri The document URI
   * @return asio::awaitable<std::shared_ptr<slang::syntax::SyntaxTree>> The
   * syntax tree or nullptr if not found
   */
  asio::awaitable<std::shared_ptr<slang::syntax::SyntaxTree>> GetSyntaxTree(
      const std::string& uri);

  /**
   * @brief Get the compilation for a document
   *
   * @param uri The document URI
   * @return asio::awaitable<std::shared_ptr<slang::ast::Compilation>> The
   * compilation or nullptr if not found
   */
  asio::awaitable<std::shared_ptr<slang::ast::Compilation>> GetCompilation(
      const std::string& uri);

  /**
   * @brief Get a list of symbols defined in a document
   *
   * @param uri The document URI
   * @return asio::awaitable<std::vector<const slang::ast::Symbol*>> List of
   * symbols or empty vector if not found
   */
  asio::awaitable<std::vector<const slang::ast::Symbol*>> GetSymbols(
      const std::string& uri);

 private:
  // ASIO io_context reference
  asio::io_context& io_context_;

  // Strand for synchronizing access to shared data
  asio::strand<asio::io_context::executor_type> strand_;

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

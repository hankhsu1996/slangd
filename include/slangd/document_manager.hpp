#pragma once

#include <expected>
#include <memory>
#include <slang/syntax/SyntaxTree.h>
#include <slang/text/SourceManager.h>
#include <string>
#include <unordered_map>

#include <asio.hpp>

namespace slangd {

/**
 * @brief Error types that can occur during document parsing
 */
enum class ParseError {
  SyntaxError,         // General syntax errors
  FileNotFound,        // File could not be found or opened
  EncodingError,       // Text encoding issues
  SlangInternalError,  // Internal error in slang library
  UnknownError         // Catch-all for other errors
};

/**
 * @brief Manages SystemVerilog documents and parsing with slang
 *
 * This class handles the SystemVerilog-specific aspects of document management,
 * including parsing files with slang and extracting symbols.
 */
class DocumentManager {
 public:
  DocumentManager(asio::io_context& io_context);
  ~DocumentManager();

  /**
   * @brief Parse a document and store its syntax tree
   *
   * @param uri The URI of the document
   * @param content The text content of the document
   * @return awaitable expected with void for success or ParseError for failure
   */
  asio::awaitable<std::expected<void, ParseError>> ParseDocument(
      const std::string& uri, const std::string& content);

  /**
   * @brief Update an existing document with new content
   *
   * @param uri The URI of the document
   * @param content The new text content
   * @return awaitable expected with void for success or ParseError for failure
   */
  asio::awaitable<std::expected<void, ParseError>> UpdateDocument(
      const std::string& uri, const std::string& content);

  /**
   * @brief Remove a document from the manager
   *
   * @param uri The URI of the document to remove
   */
  asio::awaitable<void> RemoveDocument(const std::string& uri);

  /**
   * @brief Get the syntax tree for a document
   *
   * @param uri The URI of the document
   * @return awaitable with shared_ptr to syntax tree, or nullptr if not found
   */
  asio::awaitable<std::shared_ptr<const slang::syntax::SyntaxTree>>
  GetSyntaxTree(const std::string& uri) const;

 private:
  // Source manager for slang parsing
  std::unique_ptr<slang::SourceManager> source_manager_;

  // Map of document URIs to their syntax trees
  std::unordered_map<std::string, std::shared_ptr<slang::syntax::SyntaxTree>>
      syntax_trees_;

  // Strand for synchronization
  asio::strand<asio::io_context::executor_type> strand_;
};

}  // namespace slangd

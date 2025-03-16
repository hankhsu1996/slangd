#include "slangd/document_manager.hpp"

#include <iostream>
#include <memory>
#include <slang/syntax/SyntaxTree.h>
#include <slang/text/SourceManager.h>
#include <string>

namespace slangd {

DocumentManager::DocumentManager(asio::io_context& io_context)
    : io_context_(io_context), strand_(asio::make_strand(io_context)) {
  std::cout << "DocumentManager initialized" << std::endl;
}

asio::awaitable<std::expected<void, ParseError>> DocumentManager::ParseDocument(
    const std::string& uri, const std::string& content) {
  // Ensure thread safety for data structures
  co_await asio::post(strand_, asio::use_awaitable);

  try {
    // Create a new source manager for this document if it doesn't exist
    if (source_managers_.find(uri) == source_managers_.end()) {
      source_managers_[uri] = std::make_shared<slang::SourceManager>();
    }
    auto& source_manager = source_managers_[uri];

    // Parse the document
    std::string_view content_view(content);
    auto syntax_tree =
        slang::syntax::SyntaxTree::fromText(content_view, *source_manager, uri);

    // Store the syntax tree
    syntax_trees_[uri] = syntax_tree;

    std::cout << "Successfully parsed " << uri << std::endl;
    co_return std::expected<void, ParseError>{};
  } catch (const std::exception& e) {
    std::cerr << "Error parsing document " << uri << ": " << e.what()
              << std::endl;
    co_return std::unexpected(ParseError::SlangInternalError);
  } catch (...) {
    std::cerr << "Unknown error parsing document " << uri << std::endl;
    co_return std::unexpected(ParseError::UnknownError);
  }
}

asio::awaitable<std::shared_ptr<slang::syntax::SyntaxTree>>
DocumentManager::GetSyntaxTree(const std::string& uri) {
  // Ensure thread safety
  co_await asio::post(strand_, asio::use_awaitable);

  auto it = syntax_trees_.find(uri);
  if (it != syntax_trees_.end()) {
    co_return it->second;
  }
  co_return nullptr;
}

asio::awaitable<std::shared_ptr<slang::ast::Compilation>>
DocumentManager::GetCompilation(const std::string& uri) {
  // For our minimal version, just return nullptr
  co_return nullptr;
}

asio::awaitable<std::vector<const slang::ast::Symbol*>>
DocumentManager::GetSymbols(const std::string& uri) {
  // For our minimal version, just return an empty vector
  std::vector<const slang::ast::Symbol*> symbols;
  co_return symbols;
}

}  // namespace slangd

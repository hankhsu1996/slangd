#include "slangd/document_manager.hpp"

#include <slang/syntax/SyntaxTree.h>

namespace slangd {

DocumentManager::DocumentManager(asio::io_context& io_context)
    : source_manager_(std::make_unique<slang::SourceManager>()),
      strand_(asio::make_strand(io_context)) {}

DocumentManager::~DocumentManager() {}

asio::awaitable<std::expected<void, ParseError>> DocumentManager::ParseDocument(
    const std::string& uri, const std::string& content) {
  // Switch execution to the strand
  co_await asio::post(strand_, asio::use_awaitable);

  try {
    // Parse the document
    std::string_view text(content);
    auto tree =
        slang::syntax::SyntaxTree::fromText(text, *source_manager_, uri);

    // Store the shared_ptr
    syntax_trees_[uri] = tree;

    // Return success - explicitly construct the expected
    co_return std::expected<void, ParseError>{std::in_place};
  } catch (const std::exception& e) {
    // We could examine the exception to determine the specific error type
    // For now, we'll use a generic SyntaxError for any parsing failure
    co_return std::expected<void, ParseError>{
        std::unexpect, ParseError::SyntaxError};
  }
}

asio::awaitable<std::expected<void, ParseError>>
DocumentManager::UpdateDocument(
    const std::string& uri, const std::string& content) {
  co_return co_await ParseDocument(uri, content);
}

asio::awaitable<void> DocumentManager::RemoveDocument(const std::string& uri) {
  // Switch execution to the strand
  co_await asio::post(strand_, asio::use_awaitable);

  syntax_trees_.erase(uri);
  co_return;
}

asio::awaitable<std::shared_ptr<const slang::syntax::SyntaxTree>>
DocumentManager::GetSyntaxTree(const std::string& uri) const {
  // Switch execution to the strand
  co_await asio::post(strand_, asio::use_awaitable);

  auto it = syntax_trees_.find(uri);
  if (it != syntax_trees_.end()) {
    co_return it->second;
  }

  co_return nullptr;
}

}  // namespace slangd

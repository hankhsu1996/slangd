#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

#include <asio.hpp>

#include "slangd/core/document_state.hpp"
#include "slangd/services/open_document_tracker.hpp"

namespace slangd::services {

// Thread-safe document state manager for language service layer
// Manages document content and version tracking with strand synchronization
class DocumentStateManager {
 public:
  explicit DocumentStateManager(
      asio::any_io_executor executor,
      std::shared_ptr<OpenDocumentTracker> open_tracker);

  // Update document state (awaitable for thread safety)
  auto Update(std::string uri, std::string content, int version)
      -> asio::awaitable<void>;

  // Get document state if it exists
  auto Get(std::string uri) -> asio::awaitable<std::optional<DocumentState>>;

  // Remove document state
  auto Remove(std::string uri) -> asio::awaitable<void>;

  // Check if document exists
  auto Contains(std::string uri) -> asio::awaitable<bool>;

  // Get all document URIs
  auto GetAllUris() -> asio::awaitable<std::vector<std::string>>;

 private:
  // Document storage
  std::unordered_map<std::string, DocumentState> documents_;

  // Strand for thread-safe access
  asio::strand<asio::any_io_executor> strand_;

  // Tracks which documents are open (shared with SessionManager)
  std::shared_ptr<OpenDocumentTracker> open_tracker_;
};

}  // namespace slangd::services

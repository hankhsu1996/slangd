#pragma once

#include <mutex>
#include <string>
#include <unordered_set>

namespace slangd::services {

// Tracks which documents are currently open in the editor
// Thread-safe with mutex for concurrent access from multiple managers
// Single source of truth for open/closed state - shared by composition
class OpenDocumentTracker {
 public:
  OpenDocumentTracker() = default;

  // Mark document as open
  auto Add(const std::string& uri) -> void;

  // Mark document as closed
  auto Remove(const std::string& uri) -> void;

  // Check if document is currently open
  auto Contains(const std::string& uri) const -> bool;

  // Clear all tracked documents
  auto Clear() -> void;

  // Get count of open documents (for debugging)
  auto Size() const -> size_t;

 private:
  mutable std::mutex mutex_;
  std::unordered_set<std::string> open_documents_;
};

}  // namespace slangd::services

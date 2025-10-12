#include "slangd/services/open_document_tracker.hpp"

namespace slangd::services {

auto OpenDocumentTracker::Add(const std::string& uri) -> void {
  std::lock_guard<std::mutex> lock(mutex_);
  open_documents_.insert(uri);
}

auto OpenDocumentTracker::Remove(const std::string& uri) -> void {
  std::lock_guard<std::mutex> lock(mutex_);
  open_documents_.erase(uri);
}

auto OpenDocumentTracker::Contains(const std::string& uri) const -> bool {
  std::lock_guard<std::mutex> lock(mutex_);
  return open_documents_.contains(uri);
}

auto OpenDocumentTracker::Clear() -> void {
  std::lock_guard<std::mutex> lock(mutex_);
  open_documents_.clear();
}

auto OpenDocumentTracker::Size() const -> size_t {
  std::lock_guard<std::mutex> lock(mutex_);
  return open_documents_.size();
}

}  // namespace slangd::services

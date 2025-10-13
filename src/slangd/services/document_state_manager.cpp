#include "slangd/services/document_state_manager.hpp"

namespace slangd::services {

DocumentStateManager::DocumentStateManager(
    asio::any_io_executor executor,
    std::shared_ptr<OpenDocumentTracker> open_tracker)
    : strand_(asio::make_strand(executor)),
      open_tracker_(std::move(open_tracker)) {
}

auto DocumentStateManager::Update(
    std::string uri, std::string content, int version)
    -> asio::awaitable<void> {
  co_await asio::post(strand_, asio::use_awaitable);

  documents_[uri] = DocumentState{
      .content = std::move(content),
      .version = version,
  };

  // Mark document as open
  open_tracker_->Add(uri);

  co_return;
}

auto DocumentStateManager::Get(std::string uri)
    -> asio::awaitable<std::optional<DocumentState>> {
  co_await asio::post(strand_, asio::use_awaitable);

  auto it = documents_.find(uri);
  if (it != documents_.end()) {
    co_return it->second;
  }
  co_return std::nullopt;
}

auto DocumentStateManager::Remove(std::string uri) -> asio::awaitable<void> {
  co_await asio::post(strand_, asio::use_awaitable);

  documents_.erase(uri);

  // Mark document as closed
  open_tracker_->Remove(uri);

  co_return;
}

auto DocumentStateManager::Contains(std::string uri) -> asio::awaitable<bool> {
  co_await asio::post(strand_, asio::use_awaitable);

  co_return documents_.contains(uri);
}

auto DocumentStateManager::GetAllUris()
    -> asio::awaitable<std::vector<std::string>> {
  co_await asio::post(strand_, asio::use_awaitable);

  std::vector<std::string> uris;
  uris.reserve(documents_.size());
  for (const auto& [uri, _] : documents_) {
    uris.push_back(uri);
  }
  co_return uris;
}

}  // namespace slangd::services

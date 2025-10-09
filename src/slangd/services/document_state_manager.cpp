#include "slangd/services/document_state_manager.hpp"

namespace slangd::services {

DocumentStateManager::DocumentStateManager(asio::any_io_executor executor)
    : strand_(asio::make_strand(executor)) {
}

auto DocumentStateManager::Update(
    std::string uri, std::string content, int version)
    -> asio::awaitable<void> {
  co_await asio::post(strand_, asio::use_awaitable);

  documents_[uri] = DocumentState{
      .content = std::move(content),
      .version = version,
  };
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
  co_return;
}

auto DocumentStateManager::Contains(std::string uri) -> asio::awaitable<bool> {
  co_await asio::post(strand_, asio::use_awaitable);

  co_return documents_.contains(uri);
}

}  // namespace slangd::services

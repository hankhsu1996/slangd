#include "slangd/features/definition_provider.hpp"

namespace slangd {

auto DefinitionProvider::GetDefinitionAtPosition(
    std::string uri, lsp::Position position)
    -> asio::awaitable<std::vector<lsp::Location>> {
  co_return std::vector<lsp::Location>();
}
}  // namespace slangd

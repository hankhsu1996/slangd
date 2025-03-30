#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <asio.hpp>
#include <jsonrpc/endpoint/endpoint.hpp>

#include "lsp/diagnostic.hpp"

namespace lsp {

/**
 * @brief Document content and metadata
 */
struct OpenFile {
  std::string uri;
  std::string content;
  std::string language_id;
  int version;
};

/**
 * @brief Base class for LSP servers
 *
 * This class provides the core functionality for handling Language Server
 * Protocol communication using JSON-RPC. It provides infrastructure and
 * delegates specific LSP message handling to derived classes.
 */
class LspServer {
 public:
  /**
   * @brief Constructor that accepts a pre-configured RPC endpoint
   *
   * @param io_context ASIO io_context for async operations
   * @param endpoint Pre-configured JSON-RPC endpoint
   */
  LspServer(
      asio::any_io_executor executor,
      std::unique_ptr<jsonrpc::endpoint::RpcEndpoint> endpoint);

  ~LspServer() = default;

  /**
   * @brief Initialize and start the LSP server
   *
   * This method starts the server and handles messages until shutdown
   * @return asio::awaitable<void> Awaitable that completes when server stops
   */
  auto Start() -> asio::awaitable<void>;

  /**
   * @brief Shut down the server
   *
   * @return asio::awaitable<void> Awaitable that completes when shutdown is
   * done
   */
  auto Shutdown() -> asio::awaitable<void>;

 protected:
  /**
   * @brief Register LSP method handlers
   *
   * This method should be overridden by derived classes to register
   * method handlers for specific LSP messages using the endpoint_.
   */
  virtual void RegisterHandlers() = 0;

  /**
   * @brief Publish diagnostics to the client
   *
   * @param uri Document URI
   * @param diagnostics List of diagnostics to publish
   * @param version Optional document version
   */
  auto PublishDiagnostics(const PublishDiagnosticsParams& params)
      -> asio::awaitable<void>;

  // File management helpers
  std::optional<std::reference_wrapper<OpenFile>> GetOpenFile(
      const std::string& uri);
  void AddOpenFile(
      const std::string& uri, const std::string& content,
      const std::string& language_id, int version);
  void UpdateOpenFile(
      const std::string& uri, const std::vector<std::string>& changes);
  void RemoveOpenFile(const std::string& uri);

 protected:
  std::unique_ptr<jsonrpc::endpoint::RpcEndpoint> endpoint_;
  asio::any_io_executor executor_;
  asio::executor_work_guard<asio::any_io_executor> work_guard_;

  // Map of open document URIs to their content
  std::unordered_map<std::string, OpenFile> open_files_;
};

}  // namespace lsp

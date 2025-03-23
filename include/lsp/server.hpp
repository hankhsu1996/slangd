#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <asio.hpp>
#include <jsonrpc/endpoint/endpoint.hpp>

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
class Server {
 public:
  /**
   * @brief Constructor that accepts a pre-configured RPC endpoint
   *
   * @param io_context ASIO io_context for async operations
   * @param endpoint Pre-configured JSON-RPC endpoint
   */
  Server(
      asio::io_context& io_context,
      std::unique_ptr<jsonrpc::endpoint::RpcEndpoint> endpoint);

  virtual ~Server();

  /**
   * @brief Initialize and start the LSP server
   *
   * This method starts the server and handles messages until shutdown
   * @return asio::awaitable<void> Awaitable that completes when server stops
   */
  virtual auto Run() -> asio::awaitable<void>;

  /**
   * @brief Shut down the server
   */
  virtual void Shutdown();

 protected:
  /**
   * @brief Register LSP method handlers
   *
   * This method should be overridden by derived classes to register
   * method handlers for specific LSP messages using the endpoint_.
   */
  virtual void RegisterHandlers() = 0;

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
  asio::io_context& io_context_;
  asio::executor_work_guard<asio::io_context::executor_type> work_guard_;

  // Map of open document URIs to their content
  std::unordered_map<std::string, OpenFile> open_files_;
};

}  // namespace lsp

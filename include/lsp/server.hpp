#pragma once

#include <memory>
#include <string>

// Include actual headers instead of forward declarations
#include <asio.hpp>
#include <jsonrpc/endpoint/endpoint.hpp>

namespace lsp {

/**
 * @brief Base class for LSP servers
 *
 * This class provides the core functionality for handling Language Server
 * Protocol communication using JSON-RPC.
 */
class Server {
 public:
  Server();
  virtual ~Server();

  /**
   * @brief Initialize and start the LSP server
   *
   * This method starts the server and handles messages until shutdown
   */
  virtual void Run();

  /**
   * @brief Shut down the server
   */
  virtual void Shutdown();

 protected:
  /**
   * @brief Register LSP method handlers
   *
   * This method should be overridden by derived classes to register
   * method handlers for specific LSP messages.
   */
  virtual void RegisterHandlers() = 0;

  /**
   * @brief Register a handler for an LSP method
   *
   * @param method The LSP method name
   * @param handler The handler function
   */
  void RegisterMethod(const std::string& method, void* handler);

 private:
  std::unique_ptr<jsonrpc::endpoint::RpcEndpoint> endpoint_;
  std::shared_ptr<asio::io_context> io_context_;
};

}  // namespace lsp

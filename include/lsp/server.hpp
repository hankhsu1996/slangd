#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
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
 * Protocol communication using JSON-RPC.
 */
class Server {
 public:
  Server(asio::io_context& io_context);
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

  // Core LSP request handlers
  virtual void HandleInitialize();
  virtual void HandleInitialized();
  virtual void HandleShutdown();
  virtual void HandleTextDocumentDidOpen(
      const std::string& uri, const std::string& text,
      const std::string& language_id);
  virtual void HandleTextDocumentDidChange(
      const std::string& uri, const std::vector<std::string>& changes);
  virtual void HandleTextDocumentDidClose(const std::string& uri);
  virtual void HandleTextDocumentHover(
      const std::string& uri, int line, int character);
  virtual void HandleTextDocumentDefinition(
      const std::string& uri, int line, int character);
  virtual void HandleTextDocumentCompletion(
      const std::string& uri, int line, int character);
  virtual void HandleWorkspaceSymbol(const std::string& query);

  // Initialize the JSON-RPC endpoint
  void InitializeJsonRpc();

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

  // Thread pool for background processing
  std::vector<std::thread> thread_pool_;

  // Map of open document URIs to their content
  std::unordered_map<std::string, OpenFile> open_files_;
};

}  // namespace lsp

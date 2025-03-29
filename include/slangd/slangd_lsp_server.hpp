#pragma once

#include <memory>
#include <optional>

#include <asio/awaitable.hpp>
#include <asio/io_context.hpp>
#include <asio/strand.hpp>

#include "lsp/server.hpp"
#include "slangd/document_manager.hpp"

namespace slangd {

// Forward declaration
class TestSlangdLspServer;

/**
 * SystemVerilog Language Server implementing the LSP protocol
 */
class SlangdLspServer : public lsp::Server {
 public:
  /** Constructor that accepts a pre-configured RPC endpoint. */
  SlangdLspServer(
      asio::any_io_executor executor,
      std::unique_ptr<jsonrpc::endpoint::RpcEndpoint> endpoint);

  /** Register all LSP message handlers with the JSON-RPC endpoint. */
  void RegisterHandlers() override;

 private:
  /** TestSlangdLspServer is given access to private members for testing. */
  friend class TestSlangdLspServer;

  /** Handle "initialize" request from client. */
  asio::awaitable<nlohmann::json> HandleInitialize(
      const std::optional<nlohmann::json>& params);

  /** Handle "shutdown" request from client. */
  asio::awaitable<nlohmann::json> HandleShutdown(
      const std::optional<nlohmann::json>& params);

  /** Handle "initialized" notification from client. */
  asio::awaitable<void> HandleInitialized(
      const std::optional<nlohmann::json>& params);

  /** Handle "exit" notification from client. */
  asio::awaitable<void> HandleExit(const std::optional<nlohmann::json>& params);

  /** Handle "textDocument/didOpen" notification. */
  asio::awaitable<void> HandleTextDocumentDidOpen(
      const std::optional<nlohmann::json>& params);

  /** Handle "textDocument/didChange" notification. */
  asio::awaitable<void> HandleTextDocumentDidChange(
      const std::optional<nlohmann::json>& params);

  /** Handle "textDocument/didClose" notification. */
  asio::awaitable<void> HandleTextDocumentDidClose(
      const std::optional<nlohmann::json>& params);

  /** Handle "textDocument/didSave" notification. */
  asio::awaitable<void> HandleTextDocumentDidSave(
      const std::optional<nlohmann::json>& params);

  /** Handle "textDocument/documentSymbol" request. */
  asio::awaitable<nlohmann::json> HandleTextDocumentDocumentSymbol(
      const std::optional<nlohmann::json>& params);

  // Server state
  bool initialized_ = false;
  bool shutdown_requested_ = false;

  // Thread safety
  asio::strand<asio::any_io_executor> strand_;

  // Document management
  std::unique_ptr<DocumentManager> document_manager_;
};

}  // namespace slangd

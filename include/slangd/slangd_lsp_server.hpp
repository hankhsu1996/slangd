#pragma once

#include <memory>

#include <asio/awaitable.hpp>
#include <asio/io_context.hpp>
#include <asio/strand.hpp>

#include "lsp/lifecycle.hpp"
#include "lsp/lsp_server.hpp"
#include "slangd/document_manager.hpp"
#include "slangd/workspace_manager.hpp"

namespace slangd {

// Forward declaration
class TestSlangdLspServer;

/**
 * SystemVerilog Language Server implementing the LSP protocol
 */
class SlangdLspServer : public lsp::LspServer {
 public:
  /** Constructor that accepts a pre-configured RPC endpoint. */
  SlangdLspServer(
      asio::any_io_executor executor,
      std::unique_ptr<jsonrpc::endpoint::RpcEndpoint> endpoint);

  /** Register all LSP message handlers with the JSON-RPC endpoint. */
  //   void RegisterHandlers() override;

 private:
  /** TestSlangdLspServer is given access to private members for testing. */
  friend class TestSlangdLspServer;

  // Server state
  bool initialized_ = false;
  bool shutdown_requested_ = false;

  // Thread safety
  asio::strand<asio::any_io_executor> strand_;

  // Document management
  std::unique_ptr<DocumentManager> document_manager_;

  // Workspace manager
  std::unique_ptr<WorkspaceManager> workspace_manager_;

 protected:
  // Initialize Request
  auto OnInitialize(const lsp::InitializeParams&)
      -> asio::awaitable<lsp::InitializeResult> override;

  // Initialized Notification
  auto OnInitialized(const lsp::InitializedParams&)
      -> asio::awaitable<void> override;

  // Shutdown Request
  auto OnShutdown(const lsp::ShutdownParams&)
      -> asio::awaitable<lsp::ShutdownResult> override;

  // Exit Notification
  auto OnExit(const lsp::ExitParams&) -> asio::awaitable<void> override;

  // Did Open Text Document Notification
  auto OnDidOpenTextDocument(const lsp::DidOpenTextDocumentParams&)
      -> asio::awaitable<void> override;

  // Did Change Text Document Notification
  auto OnDidChangeTextDocument(const lsp::DidChangeTextDocumentParams&)
      -> asio::awaitable<void> override;

  // Did Close Text Document Notification
  auto OnDidCloseTextDocument(const lsp::DidCloseTextDocumentParams&)
      -> asio::awaitable<void> override;

  // Document Symbols Request
  auto OnDocumentSymbols(const lsp::DocumentSymbolParams&)
      -> asio::awaitable<lsp::DocumentSymbolResult> override;
};

}  // namespace slangd

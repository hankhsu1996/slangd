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
      std::unique_ptr<jsonrpc::endpoint::RpcEndpoint> endpoint,
      std::shared_ptr<spdlog::logger> logger = nullptr);

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
  auto OnInitialize(lsp::InitializeParams) -> asio::awaitable<
      std::expected<lsp::InitializeResult, lsp::LspError>> override;

  // Initialized Notification
  auto OnInitialized(lsp::InitializedParams)
      -> asio::awaitable<std::expected<void, lsp::LspError>> override;

  // Shutdown Request
  auto OnShutdown(lsp::ShutdownParams) -> asio::awaitable<
      std::expected<lsp::ShutdownResult, lsp::LspError>> override;

  // Exit Notification
  auto OnExit(lsp::ExitParams)
      -> asio::awaitable<std::expected<void, lsp::LspError>> override;

  // Did Open Text Document Notification
  auto OnDidOpenTextDocument(lsp::DidOpenTextDocumentParams)
      -> asio::awaitable<std::expected<void, lsp::LspError>> override;

  // Did Change Text Document Notification
  auto OnDidChangeTextDocument(lsp::DidChangeTextDocumentParams)
      -> asio::awaitable<std::expected<void, lsp::LspError>> override;

  // Did Close Text Document Notification
  auto OnDidCloseTextDocument(lsp::DidCloseTextDocumentParams)
      -> asio::awaitable<std::expected<void, lsp::LspError>> override;

  // Document Symbols Request
  auto OnDocumentSymbols(lsp::DocumentSymbolParams) -> asio::awaitable<
      std::expected<lsp::DocumentSymbolResult, lsp::LspError>> override;

  // Goto Definition Request
  auto OnGotoDefinition(lsp::DefinitionParams) -> asio::awaitable<
      std::expected<lsp::DefinitionResult, lsp::LspError>> override;

  // DidChangeWatchedFiles Notification
  auto OnDidChangeWatchedFiles(lsp::DidChangeWatchedFilesParams)
      -> asio::awaitable<std::expected<void, lsp::LspError>> override;
};

}  // namespace slangd

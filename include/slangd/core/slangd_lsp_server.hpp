#pragma once

#include <memory>

#include <asio/awaitable.hpp>
#include <asio/io_context.hpp>
#include <asio/steady_timer.hpp>
#include <asio/strand.hpp>

#include "lsp/lifecycle.hpp"
#include "lsp/lsp_server.hpp"
#include "slangd/core/language_service_base.hpp"

namespace slangd {

class SlangdLspServer : public lsp::LspServer {
 public:
  SlangdLspServer(
      asio::any_io_executor executor,
      std::unique_ptr<jsonrpc::endpoint::RpcEndpoint> endpoint,
      std::shared_ptr<LanguageServiceBase> language_service,
      std::shared_ptr<spdlog::logger> logger = nullptr);

 private:
  // Server state
  bool initialized_ = false;
  bool shutdown_requested_ = false;

  // Logger
  std::shared_ptr<spdlog::logger> logger_;

  // Executor
  asio::any_io_executor executor_;

  // Language service - unified interface for all domain operations
  std::shared_ptr<LanguageServiceBase> language_service_{nullptr};

  // Workspace folder from initialize request
  std::optional<lsp::WorkspaceFolder> workspace_folder_;

  // Helper method to determine if a path is a config file
  static auto IsConfigFile(const std::string& path) -> bool;

 protected:
  // Initialize Request
  auto OnInitialize(lsp::InitializeParams params) -> asio::awaitable<
      std::expected<lsp::InitializeResult, lsp::LspError>> override;

  // Initialized Notification
  auto OnInitialized(lsp::InitializedParams params)
      -> asio::awaitable<std::expected<void, lsp::LspError>> override;

  // Shutdown Request
  auto OnShutdown(lsp::ShutdownParams params) -> asio::awaitable<
      std::expected<lsp::ShutdownResult, lsp::LspError>> override;

  // Exit Notification
  auto OnExit(lsp::ExitParams params)
      -> asio::awaitable<std::expected<void, lsp::LspError>> override;

  // Did Open Text Document Notification
  auto OnDidOpenTextDocument(lsp::DidOpenTextDocumentParams params)
      -> asio::awaitable<std::expected<void, lsp::LspError>> override;

  // Did Change Text Document Notification
  auto OnDidChangeTextDocument(lsp::DidChangeTextDocumentParams params)
      -> asio::awaitable<std::expected<void, lsp::LspError>> override;

  // Did Save Text Document Notification
  auto OnDidSaveTextDocument(lsp::DidSaveTextDocumentParams params)
      -> asio::awaitable<std::expected<void, lsp::LspError>> override;

  // Did Close Text Document Notification
  auto OnDidCloseTextDocument(lsp::DidCloseTextDocumentParams params)
      -> asio::awaitable<std::expected<void, lsp::LspError>> override;

  // Document Symbols Request
  auto OnDocumentSymbols(lsp::DocumentSymbolParams params) -> asio::awaitable<
      std::expected<lsp::DocumentSymbolResult, lsp::LspError>> override;

  // Goto Definition Request
  auto OnGotoDefinition(lsp::DefinitionParams params) -> asio::awaitable<
      std::expected<lsp::DefinitionResult, lsp::LspError>> override;

  // DidChangeWatchedFiles Notification
  auto OnDidChangeWatchedFiles(lsp::DidChangeWatchedFilesParams params)
      -> asio::awaitable<std::expected<void, lsp::LspError>> override;
};

}  // namespace slangd

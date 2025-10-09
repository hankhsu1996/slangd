#include "lsp/lsp_server.hpp"

#include <asio.hpp>
#include <jsonrpc/endpoint/endpoint.hpp>
#include <jsonrpc/transport/pipe_transport.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace lsp {

using lsp::error::LspError;
using lsp::error::Ok;

LspServer::LspServer(
    asio::any_io_executor executor,
    std::unique_ptr<jsonrpc::endpoint::RpcEndpoint> endpoint,
    std::shared_ptr<spdlog::logger> logger)
    : logger_(logger ? logger : spdlog::default_logger()),
      endpoint_(std::move(endpoint)),
      executor_(executor),
      work_guard_(asio::make_work_guard(executor)),
      file_strand_(asio::make_strand(executor)) {
}

auto LspServer::Start() -> asio::awaitable<std::expected<void, LspError>> {
  // Register method handlers
  RegisterHandlers();

  // Start the endpoint and wait for shutdown
  auto result = co_await endpoint_->Start();
  if (result.has_value()) {
    Logger()->debug("LspServer endpoint started");
  } else {
    Logger()->error("LspServer endpoint error: {}", result.error().Message());
    co_return LspError::UnexpectedFromRpcError(result.error());
  }

  // Wait for shutdown
  auto shutdown_result = co_await endpoint_->WaitForShutdown();
  if (shutdown_result.has_value()) {
    Logger()->debug("LspServer endpoint wait for shutdown completed");
  } else {
    Logger()->error(
        "LspServer endpoint wait for shutdown error: {}",
        shutdown_result.error().Message());
    co_return LspError::UnexpectedFromRpcError(shutdown_result.error());
  }

  co_return Ok();
}

auto LspServer::Shutdown() -> asio::awaitable<std::expected<void, LspError>> {
  Logger()->debug("Server shutting down");

  if (endpoint_) {
    // Directly await the endpoint shutdown
    auto result = co_await endpoint_->Shutdown();
    if (result.has_value()) {
      Logger()->debug("LspServer endpoint shutdown");
    } else {
      Logger()->error(
          "LspServer endpoint shutdown error: {}", result.error().Message());
      co_return LspError::UnexpectedFromRpcError(result.error());
    }
  }

  // Release work guard to allow threads to finish
  work_guard_.reset();

  co_return Ok();
}

void LspServer::RegisterHandlers() {
  RegisterLifecycleHandlers();
  RegisterDocumentSyncHandlers();
  RegisterLanguageFeatureHandlers();
  RegisterWorkspaceFeatureHandlers();
  RegisterWindowFeatureHandlers();
}

void LspServer::RegisterLifecycleHandlers() {
  // Initialize Request
  endpoint_->RegisterMethodCall<InitializeParams, InitializeResult, LspError>(
      "initialize",
      [this](const InitializeParams& params) { return OnInitialize(params); });

  // Initialized Notification
  endpoint_->RegisterNotification<InitializedParams, LspError>(
      "initialized", [this](const InitializedParams& params) {
        return OnInitialized(params);
      });

  // SetTrace Notification
  endpoint_->RegisterNotification<SetTraceParams, LspError>(
      "$/setTrace",
      [this](const SetTraceParams& params) { return OnSetTrace(params); });

  // LogTrace Notification
  endpoint_->RegisterNotification<LogTraceParams, LspError>(
      "$/logTrace",
      [this](const LogTraceParams& params) { return OnLogTrace(params); });

  // Shutdown Request
  endpoint_->RegisterMethodCall<ShutdownParams, ShutdownResult, LspError>(
      "shutdown",
      [this](const ShutdownParams& params) { return OnShutdown(params); });

  // Exit Notification
  endpoint_->RegisterNotification<ExitParams, LspError>(
      "exit", [this](const ExitParams& params) { return OnExit(params); });
}

void LspServer::RegisterDocumentSyncHandlers() {
  // DidOpenTextDocument Notification
  endpoint_->RegisterNotification<DidOpenTextDocumentParams, LspError>(
      "textDocument/didOpen", [this](const DidOpenTextDocumentParams& params) {
        return OnDidOpenTextDocument(params);
      });

  // DidChangeTextDocument Notification
  endpoint_->RegisterNotification<DidChangeTextDocumentParams, LspError>(
      "textDocument/didChange",
      [this](const DidChangeTextDocumentParams& params) {
        return OnDidChangeTextDocument(params);
      });

  // WillSaveTextDocument Notification
  endpoint_->RegisterNotification<WillSaveTextDocumentParams, LspError>(
      "textDocument/willSave",
      [this](const WillSaveTextDocumentParams& params) {
        return OnWillSaveTextDocument(params);
      });

  // WillSaveWaitUntilTextDocument Request
  endpoint_->RegisterMethodCall<
      WillSaveTextDocumentParams, WillSaveTextDocumentResult, LspError>(
      "textDocument/willSaveWaitUntil",
      [this](const WillSaveTextDocumentParams& params) {
        return OnWillSaveWaitUntilTextDocument(params);
      });

  // DidSaveTextDocument Notification
  endpoint_->RegisterNotification<DidSaveTextDocumentParams, LspError>(
      "textDocument/didSave", [this](const DidSaveTextDocumentParams& params) {
        return OnDidSaveTextDocument(params);
      });

  // DidCloseTextDocument Notification
  endpoint_->RegisterNotification<DidCloseTextDocumentParams, LspError>(
      "textDocument/didClose",
      [this](const DidCloseTextDocumentParams& params) {
        return OnDidCloseTextDocument(params);
      });

  // TODO(hankhsu1996): Did Open Notebook Document
  // TODO(hankhsu1996): Did Change Notebook Document
  // TODO(hankhsu1996): Did Save Notebook Document
  // TODO(hankhsu1996): Did Close Notebook Document
}

void LspServer::RegisterLanguageFeatureHandlers() {
  // TODO(hankhsu1996): Go to Declaration

  // Goto Definition Request
  endpoint_->RegisterMethodCall<DefinitionParams, DefinitionResult, LspError>(
      "textDocument/definition", [this](const DefinitionParams& params) {
        return OnGotoDefinition(params);
      });

  // TODO(hankhsu1996): Go to Type Definition
  // TODO(hankhsu1996): Go to Implementation
  // TODO(hankhsu1996): Find References
  // TODO(hankhsu1996): Prepare Call Hierarchy
  // TODO(hankhsu1996): Call Hierarchy Incoming Calls
  // TODO(hankhsu1996): Call Hierarchy Outgoing Calls
  // TODO(hankhsu1996): Prepare Type Hierarchy
  // TODO(hankhsu1996): Type Hierarchy Super Types
  // TODO(hankhsu1996): Type Hierarchy Sub Types
  // TODO(hankhsu1996): Document Highlight
  // TODO(hankhsu1996): Document Link
  // TODO(hankhsu1996): Document Link Resolve
  // TODO(hankhsu1996): Hover
  // TODO(hankhsu1996): Code Lens
  // TODO(hankhsu1996): Code Lens Refresh
  // TODO(hankhsu1996): Folding Range
  // TODO(hankhsu1996): Selection Range

  // Document Symbols Request
  endpoint_->RegisterMethodCall<
      DocumentSymbolParams, DocumentSymbolResult, LspError>(
      "textDocument/documentSymbol",
      [this](const DocumentSymbolParams& params) {
        return OnDocumentSymbols(params);
      });

  // TODO(hankhsu1996): Semantic Tokens
  // TODO(hankhsu1996): Inline Value
  // TODO(hankhsu1996): Inline Value Refresh
  // TODO(hankhsu1996): Inlay Hint
  // TODO(hankhsu1996): Inlay Hint Resolve
  // TODO(hankhsu1996): Inlay Hint Refresh
  // TODO(hankhsu1996): Moniker
  // TODO(hankhsu1996): Completion Proposals
  // TODO(hankhsu1996): Completion Item Resolve
  // TODO(hankhsu1996): Pull Diagnostics
  // TODO(hankhsu1996): Signature Help
  // TODO(hankhsu1996): Code Action
  // TODO(hankhsu1996): Code Action Resolve
  // TODO(hankhsu1996): Document Color
  // TODO(hankhsu1996): Color Presentation
  // TODO(hankhsu1996): Formatting
  // TODO(hankhsu1996): Range Formatting
  // TODO(hankhsu1996): On type Formatting
  // TODO(hankhsu1996): Rename
  // TODO(hankhsu1996): Prepare Rename
  // TODO(hankhsu1996): Linked Editing Range
}

void LspServer::RegisterWorkspaceFeatureHandlers() {
  // TODO(hankhsu1996): Workspace Symbols
  // TODO(hankhsu1996): Workspace Symbol Resolve
  // TODO(hankhsu1996): Get Configuration
  // TODO(hankhsu1996): Did Change Configuration
  // TODO(hankhsu1996): Workspace Folders
  // TODO(hankhsu1996): Did Change Workspace Folders
  // TODO(hankhsu1996): Will Create Files
  // TODO(hankhsu1996): Did Create Files
  // TODO(hankhsu1996): Will Rename Files
  // TODO(hankhsu1996): Did Rename Files
  // TODO(hankhsu1996): Will Delete Files
  // TODO(hankhsu1996): Did Delete Files

  // DidChangeWatchedFiles Notification
  endpoint_->RegisterNotification<DidChangeWatchedFilesParams, LspError>(
      "workspace/didChangeWatchedFiles",
      [this](const DidChangeWatchedFilesParams& params) {
        return OnDidChangeWatchedFiles(params);
      });

  // TODO(hankhsu1996): Execute Command
  // TODO(hankhsu1996): Apply Edit
}

void LspServer::RegisterWindowFeatureHandlers() {
}

// File management helpers
auto LspServer::GetOpenFile(std::string uri)
    -> asio::awaitable<std::optional<std::reference_wrapper<OpenFile>>> {
  co_await asio::post(file_strand_, asio::use_awaitable);

  auto it = open_files_.find(uri);
  if (it != open_files_.end()) {
    co_return std::ref(it->second);
  }
  co_return std::nullopt;
}

auto LspServer::AddOpenFile(
    std::string uri, std::string content, std::string language_id, int version)
    -> asio::awaitable<void> {
  co_await asio::post(file_strand_, asio::use_awaitable);

  Logger()->debug("LspServer adding open file: {}", uri);
  OpenFile file{
      .uri = uri,
      .content = content,
      .language_id = language_id,
      .version = version,
  };
  open_files_[uri] = std::move(file);
  co_return;
}

auto LspServer::UpdateOpenFile(
    std::string uri, std::string content, int version)
    -> asio::awaitable<void> {
  co_await asio::post(file_strand_, asio::use_awaitable);

  Logger()->debug("LspServer updating open file: {}", uri);
  auto it = open_files_.find(uri);
  if (it != open_files_.end()) {
    OpenFile& file = it->second;
    file.content = std::move(content);
    file.version = version;
    Logger()->debug(
        "LspServer updated file {} to version {}", uri, file.version);
  }
  co_return;
}

auto LspServer::RemoveOpenFile(std::string uri) -> asio::awaitable<void> {
  co_await asio::post(file_strand_, asio::use_awaitable);

  Logger()->debug("LspServer removing open file: {}", uri);
  open_files_.erase(uri);
  co_return;
}

}  // namespace lsp

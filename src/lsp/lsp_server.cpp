#include "lsp/lsp_server.hpp"

#include <asio.hpp>
#include <jsonrpc/endpoint/endpoint.hpp>
#include <jsonrpc/transport/pipe_transport.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace lsp {

LspServer::LspServer(
    asio::any_io_executor executor,
    std::unique_ptr<jsonrpc::endpoint::RpcEndpoint> endpoint)
    : endpoint_(std::move(endpoint)),
      executor_(executor),
      work_guard_(asio::make_work_guard(executor)) {}

auto LspServer::Start() -> asio::awaitable<std::expected<void, RpcError>> {
  // Register method handlers
  RegisterHandlers();

  // Start the endpoint and wait for shutdown
  auto result = co_await endpoint_->Start();
  if (result.has_value()) {
    spdlog::debug("LspServer endpoint started");
  } else {
    spdlog::error("LspServer endpoint error: {}", result.error().message);
    co_return std::unexpected(result.error());
  }

  // Wait for shutdown
  auto shutdown_result = co_await endpoint_->WaitForShutdown();
  if (shutdown_result.has_value()) {
    spdlog::debug("LspServer endpoint wait for shutdown completed");
  } else {
    spdlog::error(
        "LspServer endpoint wait for shutdown error: {}",
        shutdown_result.error().message);
    co_return std::unexpected(shutdown_result.error());
  }

  co_return std::expected<void, RpcError>{};
}

auto LspServer::Shutdown() -> asio::awaitable<std::expected<void, RpcError>> {
  spdlog::debug("Server shutting down");

  if (endpoint_) {
    // Directly await the endpoint shutdown
    auto result = co_await endpoint_->Shutdown();
    if (result.has_value()) {
      spdlog::debug("LspServer endpoint shutdown");
    } else {
      spdlog::error(
          "LspServer endpoint shutdown error: {}", result.error().message);
      co_return std::unexpected(result.error());
    }
  }

  // Release work guard to allow threads to finish
  work_guard_.reset();

  co_return std::expected<void, RpcError>{};
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
  endpoint_->RegisterMethodCall<InitializeParams, InitializeResult>(
      "initialize",
      [this](const InitializeParams& params) { return OnInitialize(params); });

  // Initialized Notification
  endpoint_->RegisterNotification<InitializedParams>(
      "initialized", [this](const InitializedParams& params) {
        return OnInitialized(params);
      });

  // SetTrace Notification
  endpoint_->RegisterNotification<SetTraceParams>(
      "$/setTrace",
      [this](const SetTraceParams& params) { return OnSetTrace(params); });

  // LogTrace Notification
  endpoint_->RegisterNotification<LogTraceParams>(
      "$/logTrace",
      [this](const LogTraceParams& params) { return OnLogTrace(params); });

  // Shutdown Request
  endpoint_->RegisterMethodCall<ShutdownParams, ShutdownResult>(
      "shutdown",
      [this](const ShutdownParams& params) { return OnShutdown(params); });

  // Exit Notification
  endpoint_->RegisterNotification<ExitParams>(
      "exit", [this](const ExitParams& params) { return OnExit(params); });
}

void LspServer::RegisterDocumentSyncHandlers() {
  // DidOpenTextDocument Notification
  endpoint_->RegisterNotification<DidOpenTextDocumentParams>(
      "textDocument/didOpen", [this](const DidOpenTextDocumentParams& params) {
        return OnDidOpenTextDocument(params);
      });

  // DidChangeTextDocument Notification
  endpoint_->RegisterNotification<DidChangeTextDocumentParams>(
      "textDocument/didChange",
      [this](const DidChangeTextDocumentParams& params) {
        return OnDidChangeTextDocument(params);
      });

  // WillSaveTextDocument Notification
  endpoint_->RegisterNotification<WillSaveTextDocumentParams>(
      "textDocument/willSave",
      [this](const WillSaveTextDocumentParams& params) {
        return OnWillSaveTextDocument(params);
      });

  // WillSaveWaitUntilTextDocument Request
  endpoint_->RegisterMethodCall<
      WillSaveTextDocumentParams, WillSaveTextDocumentResult>(
      "textDocument/willSaveWaitUntil",
      [this](const WillSaveTextDocumentParams& params) {
        return OnWillSaveWaitUntilTextDocument(params);
      });

  // DidSaveTextDocument Notification
  endpoint_->RegisterNotification<DidSaveTextDocumentParams>(
      "textDocument/didSave", [this](const DidSaveTextDocumentParams& params) {
        return OnDidSaveTextDocument(params);
      });

  // DidCloseTextDocument Notification
  endpoint_->RegisterNotification<DidCloseTextDocumentParams>(
      "textDocument/didClose",
      [this](const DidCloseTextDocumentParams& params) {
        return OnDidCloseTextDocument(params);
      });

  // TODO: Did Open Notebook Document
  // TODO: Did Change Notebook Document
  // TODO: Did Save Notebook Document
  // TODO: Did Close Notebook Document
}

void LspServer::RegisterLanguageFeatureHandlers() {
  // TODO: Go to Declaration
  // TODO: Go to Definition
  // TODO: Go to Type Definition
  // TODO: Go to Implementation
  // TODO: Find References
  // TODO: Prepare Call Hierarchy
  // TODO: Call Hierarchy Incoming Calls
  // TODO: Call Hierarchy Outgoing Calls
  // TODO: Prepare Type Hierarchy
  // TODO: Type Hierarchy Super Types
  // TODO: Type Hierarchy Sub Types
  // TODO: Document Highlight
  // TODO: Document Link
  // TODO: Document Link Resolve
  // TODO: Hover
  // TODO: Code Lens
  // TODO: Code Lens Refresh
  // TODO: Folding Range
  // TODO: Selection Range

  // Document Symbols Request
  endpoint_->RegisterMethodCall<DocumentSymbolParams, DocumentSymbolResult>(
      "textDocument/documentSymbol",
      [this](const DocumentSymbolParams& params) {
        return OnDocumentSymbols(params);
      });

  // TODO: Semantic Tokens
  // TODO: Inline Value
  // TODO: Inline Value Refresh
  // TODO: Inlay Hint
  // TODO: Inlay Hint Resolve
  // TODO: Inlay Hint Refresh
  // TODO: Moniker
  // TODO: Completion Proposals
  // TODO: Completion Item Resolve
  // TODO: Pull Diagnostics
  // TODO: Signature Help
  // TODO: Code Action
  // TODO: Code Action Resolve
  // TODO: Document Color
  // TODO: Color Presentation
  // TODO: Formatting
  // TODO: Range Formatting
  // TODO: On type Formatting
  // TODO: Rename
  // TODO: Prepare Rename
  // TODO: Linked Editing Range
}

void LspServer::RegisterWorkspaceFeatureHandlers() {}

void LspServer::RegisterWindowFeatureHandlers() {}

// File management helpers
std::optional<std::reference_wrapper<OpenFile>> LspServer::GetOpenFile(
    const std::string& uri) {
  auto it = open_files_.find(uri);
  if (it != open_files_.end()) {
    return std::ref(it->second);
  }
  return std::nullopt;
}

void LspServer::AddOpenFile(
    const std::string& uri, const std::string& content,
    const std::string& language_id, int version) {
  spdlog::debug("LspServer adding open file: {}", uri);
  OpenFile file{uri, content, language_id, version};
  open_files_[uri] = std::move(file);
}

void LspServer::UpdateOpenFile(
    const std::string& uri, const std::vector<std::string>& /*changes*/) {
  spdlog::debug("LspServer updating open file: {}", uri);
  auto file_opt = GetOpenFile(uri);
  if (file_opt) {
    // Simplified update - in a real implementation, we would apply the changes
    // Here we just increment the version
    OpenFile& file = file_opt->get();
    file.version++;
    spdlog::debug("LspServer updated file {} to version {}", uri, file.version);
  }
}

void LspServer::RemoveOpenFile(const std::string& uri) {
  spdlog::debug("LspServer removing open file: {}", uri);
  open_files_.erase(uri);
}

}  // namespace lsp

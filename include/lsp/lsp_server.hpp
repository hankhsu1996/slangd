#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <asio.hpp>
#include <jsonrpc/endpoint/endpoint.hpp>
#include <jsonrpc/error/error.hpp>

#include "lsp/diagnostic.hpp"
#include "lsp/document_features.hpp"
#include "lsp/document_sync.hpp"
#include "lsp/error.hpp"
#include "lsp/lifecycle.hpp"

namespace lsp {

using lsp::error::LspError;
using lsp::error::LspErrorCode;
using lsp::error::Ok;

struct OpenFile {
  std::string uri;
  std::string content;
  std::string language_id;
  int version;
};

class LspServer {
 public:
  LspServer(
      asio::any_io_executor executor,
      std::unique_ptr<jsonrpc::endpoint::RpcEndpoint> endpoint);

  LspServer(const LspServer&) = delete;
  LspServer(LspServer&&) = delete;
  auto operator=(const LspServer&) -> LspServer& = delete;
  auto operator=(LspServer&&) -> LspServer& = delete;

  virtual ~LspServer() = default;

  auto Start() -> asio::awaitable<std::expected<void, LspError>>;
  auto Shutdown() -> asio::awaitable<std::expected<void, LspError>>;

 protected:
  void RegisterHandlers();

  // File management helpers
  auto GetOpenFile(const std::string& uri)
      -> std::optional<std::reference_wrapper<OpenFile>>;
  void AddOpenFile(
      const std::string& uri, const std::string& content,
      const std::string& language_id, int version);
  void UpdateOpenFile(
      const std::string& uri, const std::vector<std::string>& changes);
  void RemoveOpenFile(const std::string& uri);

 private:
  std::unique_ptr<jsonrpc::endpoint::RpcEndpoint> endpoint_;
  asio::any_io_executor executor_;
  asio::executor_work_guard<asio::any_io_executor> work_guard_;

  // Map of open document URIs to their content
  std::unordered_map<std::string, OpenFile> open_files_;

  void RegisterLifecycleHandlers();
  void RegisterDocumentSyncHandlers();
  void RegisterLanguageFeatureHandlers();
  void RegisterWorkspaceFeatureHandlers();
  void RegisterWindowFeatureHandlers();

 protected:
  // Initialize Request
  virtual auto OnInitialize(InitializeParams /*unused*/)
      -> asio::awaitable<std::expected<InitializeResult, LspError>> {
    co_return LspError::UnexpectedFromCode(LspErrorCode::kMethodNotImplemented);
  }

  // Initialized Notification
  virtual auto OnInitialized(InitializedParams /*unused*/)
      -> asio::awaitable<std::expected<void, LspError>> {
    co_return LspError::UnexpectedFromCode(LspErrorCode::kMethodNotImplemented);
  }

  // Register Capability
  auto RegisterCapability(RegistrationParams params)
      -> asio::awaitable<std::expected<RegistrationResult, LspError>> {
    auto result = co_await endpoint_
                      ->SendMethodCall<RegistrationParams, RegistrationResult>(
                          "client/registerCapability", params);
    if (!result) {
      spdlog::error(
          "LspServer failed to register capability: {}",
          result.error().Message());
      co_return LspError::UnexpectedFromRpcError(result.error());
    }
    co_return result.value();
  }

  // Unregister Capability
  auto UnregisterCapability(UnregistrationParams params)
      -> asio::awaitable<std::expected<UnregistrationResult, LspError>> {
    auto result =
        co_await endpoint_
            ->SendMethodCall<UnregistrationParams, UnregistrationResult>(
                "client/unregisterCapability", params);
    if (!result) {
      spdlog::error(
          "LspServer failed to unregister capability: {}",
          result.error().Message());
      co_return LspError::UnexpectedFromRpcError(result.error());
    }
    co_return result.value();
  }

  // SetTrace Notification
  virtual auto OnSetTrace(SetTraceParams /*unused*/)
      -> asio::awaitable<std::expected<void, LspError>> {
    co_return LspError::UnexpectedFromCode(LspErrorCode::kMethodNotImplemented);
  }

  // LogTrace Notification
  virtual auto OnLogTrace(LogTraceParams /*unused*/)
      -> asio::awaitable<std::expected<void, LspError>> {
    co_return LspError::UnexpectedFromCode(LspErrorCode::kMethodNotImplemented);
  }

  // Shutdown Request
  virtual auto OnShutdown(ShutdownParams /*unused*/)
      -> asio::awaitable<std::expected<ShutdownResult, LspError>> {
    co_return LspError::UnexpectedFromCode(LspErrorCode::kMethodNotImplemented);
  }

  // Exit Notification
  virtual auto OnExit(ExitParams /*unused*/)
      -> asio::awaitable<std::expected<void, LspError>> {
    co_return LspError::UnexpectedFromCode(LspErrorCode::kMethodNotImplemented);
  }

  // Document Synchronization Handlers
  virtual auto OnDidOpenTextDocument(DidOpenTextDocumentParams /*unused*/)
      -> asio::awaitable<std::expected<void, LspError>> {
    co_return LspError::UnexpectedFromCode(LspErrorCode::kMethodNotImplemented);
  }

  // DidChangeTextDocument Notification
  virtual auto OnDidChangeTextDocument(DidChangeTextDocumentParams /*unused*/)
      -> asio::awaitable<std::expected<void, LspError>> {
    co_return LspError::UnexpectedFromCode(LspErrorCode::kMethodNotImplemented);
  }

  // WillSaveTextDocument Notification
  virtual auto OnWillSaveTextDocument(WillSaveTextDocumentParams /*unused*/)
      -> asio::awaitable<std::expected<void, LspError>> {
    co_return LspError::UnexpectedFromCode(LspErrorCode::kMethodNotImplemented);
  }

  // WillSaveWaitUntilTextDocument Request
  virtual auto OnWillSaveWaitUntilTextDocument(
      WillSaveTextDocumentParams /*unused*/)
      -> asio::awaitable<std::expected<WillSaveTextDocumentResult, LspError>> {
    co_return LspError::UnexpectedFromCode(LspErrorCode::kMethodNotImplemented);
  }

  // DidSaveTextDocument Notification
  virtual auto OnDidSaveTextDocument(DidSaveTextDocumentParams /*unused*/)
      -> asio::awaitable<std::expected<void, LspError>> {
    co_return LspError::UnexpectedFromCode(LspErrorCode::kMethodNotImplemented);
  }

  // DidCloseTextDocument Notification
  virtual auto OnDidCloseTextDocument(DidCloseTextDocumentParams /*unused*/)
      -> asio::awaitable<std::expected<void, LspError>> {
    co_return LspError::UnexpectedFromCode(LspErrorCode::kMethodNotImplemented);
  }

  // TODO(hankhsu1996): Did Open Notebook Document
  // TODO(hankhsu1996): Did Change Notebook Document
  // TODO(hankhsu1996): Did Save Notebook Document
  // TODO(hankhsu1996): Did Close Notebook Document
  // TODO(hankhsu1996): Go to Declaration
  // TODO(hankhsu1996): Go to Definition
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
  virtual auto OnDocumentSymbols(DocumentSymbolParams /*unused*/)
      -> asio::awaitable<std::expected<DocumentSymbolResult, LspError>> {
    co_return LspError::UnexpectedFromCode(LspErrorCode::kMethodNotImplemented);
  }

  // TODO(hankhsu1996): Semantic Tokens
  // TODO(hankhsu1996): Inline Value
  // TODO(hankhsu1996): Inline Value Refresh
  // TODO(hankhsu1996): Inlay Hint
  // TODO(hankhsu1996): Inlay Hint Resolve
  // TODO(hankhsu1996): Inlay Hint Refresh
  // TODO(hankhsu1996): Moniker
  // TODO(hankhsu1996): Completion Proposals
  // TODO(hankhsu1996): Completion Item Resolve

  // PublishDiagnostics Notification
  auto PublishDiagnostics(PublishDiagnosticsParams params)
      -> asio::awaitable<std::expected<void, LspError>> {
    spdlog::debug(
        "LspServer publishing {} diagnostics for {}", params.diagnostics.size(),
        params.uri);
    auto result =
        co_await endpoint_->SendNotification<PublishDiagnosticsParams>(
            "textDocument/publishDiagnostics", params);
    if (!result) {
      spdlog::error(
          "LspServer failed to publish diagnostics: {}",
          result.error().Message());
      co_return LspError::UnexpectedFromRpcError(result.error());
    }
    co_return Ok();
  }

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
};

}  // namespace lsp

#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <asio.hpp>
#include <jsonrpc/endpoint/endpoint.hpp>

#include "lsp/diagnostic.hpp"
#include "lsp/document_features.hpp"
#include "lsp/document_sync.hpp"
#include "lsp/lifecycle.hpp"

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
class LspServer {
 public:
  /**
   * @brief Constructor that accepts a pre-configured RPC endpoint
   *
   * @param io_context ASIO io_context for async operations
   * @param endpoint Pre-configured JSON-RPC endpoint
   */
  LspServer(
      asio::any_io_executor executor,
      std::unique_ptr<jsonrpc::endpoint::RpcEndpoint> endpoint);

  ~LspServer() = default;

  /**
   * @brief Initialize and start the LSP server
   *
   * This method starts the server and handles messages until shutdown
   * @return asio::awaitable<void> Awaitable that completes when server stops
   */
  auto Start() -> asio::awaitable<void>;

  /**
   * @brief Shut down the server
   *
   * @return asio::awaitable<void> Awaitable that completes when shutdown is
   * done
   */
  auto Shutdown() -> asio::awaitable<void>;

 protected:
  /**
   * @brief Register LSP method handlers
   *
   * This method should be overridden by derived classes to register
   * method handlers for specific LSP messages using the endpoint_.
   */
  void RegisterHandlers();

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
  asio::any_io_executor executor_;
  asio::executor_work_guard<asio::any_io_executor> work_guard_;

  // Map of open document URIs to their content
  std::unordered_map<std::string, OpenFile> open_files_;

 private:
  void RegisterLifecycleHandlers();
  void RegisterDocumentSyncHandlers();
  void RegisterLanguageFeatureHandlers();
  void RegisterWorkspaceFeatureHandlers();
  void RegisterWindowFeatureHandlers();

 protected:
  // Initialize Request
  virtual auto OnInitialize(const InitializeParams&)
      -> asio::awaitable<InitializeResult> {
    throw std::runtime_error("Not implemented");
  }

  // Initialized Notification
  virtual auto OnInitialized(const InitializedParams&)
      -> asio::awaitable<void> {
    throw std::runtime_error("Not implemented");
  }

  // Register Capability
  virtual auto OnRegisterCapability(const RegistrationParams&)
      -> asio::awaitable<RegistrationParams> {
    throw std::runtime_error("Not implemented");
  }

  // Unregister Capability
  virtual auto OnUnregisterCapability(const UnregistrationParams&)
      -> asio::awaitable<UnregistrationParams> {
    throw std::runtime_error("Not implemented");
  }

  // SetTrace Notification
  virtual auto OnSetTrace(const SetTraceParams&) -> asio::awaitable<void> {
    throw std::runtime_error("Not implemented");
  }

  // LogTrace Notification
  virtual auto OnLogTrace(const LogTraceParams&) -> asio::awaitable<void> {
    throw std::runtime_error("Not implemented");
  }

  // Shutdown Request
  virtual auto OnShutdown(const ShutdownParams&)
      -> asio::awaitable<ShutdownResult> {
    throw std::runtime_error("Not implemented");
  }

  // Exit Notification
  virtual auto OnExit(const ExitParams&) -> asio::awaitable<void> {
    throw std::runtime_error("Not implemented");
  }

  // Document Synchronization Handlers
  virtual auto OnDidOpenTextDocument(const DidOpenTextDocumentParams&)
      -> asio::awaitable<void> {
    throw std::runtime_error("Not implemented");
  }

  // DidChangeTextDocument Notification
  virtual auto OnDidChangeTextDocument(const DidChangeTextDocumentParams&)
      -> asio::awaitable<void> {
    throw std::runtime_error("Not implemented");
  }

  // WillSaveTextDocument Notification
  virtual auto OnWillSaveTextDocument(const WillSaveTextDocumentParams&)
      -> asio::awaitable<void> {
    throw std::runtime_error("Not implemented");
  }

  // WillSaveWaitUntilTextDocument Request
  virtual auto OnWillSaveWaitUntilTextDocument(
      const WillSaveTextDocumentParams&)
      -> asio::awaitable<WillSaveTextDocumentResult> {
    throw std::runtime_error("Not implemented");
  }

  // DidSaveTextDocument Notification
  virtual auto OnDidSaveTextDocument(const DidSaveTextDocumentParams&)
      -> asio::awaitable<void> {
    throw std::runtime_error("Not implemented");
  }

  // DidCloseTextDocument Notification
  virtual auto OnDidCloseTextDocument(const DidCloseTextDocumentParams&)
      -> asio::awaitable<void> {
    throw std::runtime_error("Not implemented");
  }

  // TODO: Did Open Notebook Document
  // TODO: Did Change Notebook Document
  // TODO: Did Save Notebook Document
  // TODO: Did Close Notebook Document
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
  virtual auto OnDocumentSymbols(const DocumentSymbolParams&)
      -> asio::awaitable<DocumentSymbolResult> {
    throw std::runtime_error("Not implemented");
  }

  // TODO: Semantic Tokens
  // TODO: Inline Value
  // TODO: Inline Value Refresh
  // TODO: Inlay Hint
  // TODO: Inlay Hint Resolve
  // TODO: Inlay Hint Refresh
  // TODO: Moniker
  // TODO: Completion Proposals
  // TODO: Completion Item Resolve

  // PublishDiagnostics Notification
  auto PublishDiagnostics(const PublishDiagnosticsParams& params)
      -> asio::awaitable<void> {
    co_await endpoint_->SendNotification(
        "textDocument/publishDiagnostics", nlohmann::json(params));
  }

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
};

}  // namespace lsp

#include "slangd/slangd_lsp_server.hpp"

#include <slang/syntax/AllSyntax.h>
#include <slang/syntax/SyntaxVisitor.h>
#include <spdlog/spdlog.h>

namespace slangd {

SlangdLspServer::SlangdLspServer(
    asio::io_context& io_context,
    std::unique_ptr<jsonrpc::endpoint::RpcEndpoint> endpoint)
    : lsp::Server(io_context, std::move(endpoint)),
      strand_(asio::make_strand(io_context)) {
  // Initialize the document manager with a reference to io_context
  document_manager_ = std::make_unique<DocumentManager>(io_context);
}

SlangdLspServer::~SlangdLspServer() {}

void SlangdLspServer::RegisterHandlers() {
  // Register standard LSP methods with proper handler implementation
  endpoint_->RegisterMethodCall(
      "initialize",
      [this](const std::optional<nlohmann::json>& params)
          -> asio::awaitable<nlohmann::json> {
        // Delegate to the handler method
        return HandleInitialize(params);
      });

  // Add shutdown handler
  endpoint_->RegisterMethodCall(
      "shutdown",
      [this](const std::optional<nlohmann::json>& params)
          -> asio::awaitable<nlohmann::json> {
        // Delegate to the handler method
        return HandleShutdown(params);
      });

  // Add exit notification handler
  endpoint_->RegisterNotification(
      "exit",
      [this](const std::optional<nlohmann::json>& params)
          -> asio::awaitable<void> {
        // Delegate to the handler method
        return HandleExit(params);
      });

  // Add initialized notification handler
  endpoint_->RegisterNotification(
      "initialized",
      [this](const std::optional<nlohmann::json>& params)
          -> asio::awaitable<void> {
        // Delegate to the handler method
        return HandleInitialized(params);
      });

  // Register notifications for document synchronization
  endpoint_->RegisterNotification(
      "textDocument/didOpen",
      [this](const std::optional<nlohmann::json>& params)
          -> asio::awaitable<void> {
        return HandleTextDocumentDidOpen(params);
      });

  endpoint_->RegisterNotification(
      "textDocument/didChange",
      [this](const std::optional<nlohmann::json>& params)
          -> asio::awaitable<void> {
        return HandleTextDocumentDidChange(params);
      });

  endpoint_->RegisterNotification(
      "textDocument/didClose",
      [this](const std::optional<nlohmann::json>& params)
          -> asio::awaitable<void> {
        return HandleTextDocumentDidClose(params);
      });

  // Register document symbol handler
  endpoint_->RegisterMethodCall(
      "textDocument/documentSymbol",
      [this](const std::optional<nlohmann::json>& params)
          -> asio::awaitable<nlohmann::json> {
        return HandleTextDocumentDocumentSymbol(params);
      });
}

asio::awaitable<nlohmann::json> SlangdLspServer::HandleInitialize(
    const std::optional<nlohmann::json>& params) {
  // Return initialize result with updated capabilities
  nlohmann::json capabilities = {
      {"textDocumentSync", 1},          // 1 = Full sync
      {"documentSymbolProvider", true}  // Support document symbols
  };

  nlohmann::json result = {
      {"capabilities", capabilities},
      {"serverInfo", {{"name", "slangd"}, {"version", "0.1.0"}}}};

  co_return result;
}

asio::awaitable<nlohmann::json> SlangdLspServer::HandleShutdown(
    const std::optional<nlohmann::json>& params) {
  // Set shutdown flag to indicate server is shutting down
  shutdown_requested_ = true;

  spdlog::info("SlangdLspServer shutdown request received");

  // Return empty/null result as per LSP spec
  co_return nullptr;
}

asio::awaitable<void> SlangdLspServer::HandleInitialized(
    const std::optional<nlohmann::json>& params) {
  // Mark server as initialized
  initialized_ = true;

  spdlog::info("SlangdLspServer initialized notification received");

  // This would be a good place to send server-initiated requests
  // such as workspace configuration requests or client registrations

  co_return;
}

asio::awaitable<void> SlangdLspServer::HandleExit(
    const std::optional<nlohmann::json>& params) {
  // If shutdown was called before, exit with code 0
  // Otherwise, exit with error code 1 as per LSP spec
  int exit_code = shutdown_requested_ ? 0 : 1;

  spdlog::info(
      "SlangdLspServer exit notification received, will exit with code: {}",
      exit_code);

  // Clean up resources using base class Shutdown
  co_await lsp::Server::Shutdown();

  // Signal io_context to stop
  io_context_.stop();

  co_return;
}

asio::awaitable<void> SlangdLspServer::HandleTextDocumentDidOpen(
    const std::optional<nlohmann::json>& params) {
  if (!params) {
    co_return;
  }

  const auto& param_val = params.value();
  const auto& uri = param_val["textDocument"]["uri"].get<std::string>();
  const auto& text = param_val["textDocument"]["text"].get<std::string>();
  const auto& language_id =
      param_val["textDocument"]["languageId"].get<std::string>();

  // Use file management helpers directly
  AddOpenFile(uri, text, language_id, 1);
  spdlog::info("Document opened: {}", uri);

  // Post to strand to ensure thread safety
  asio::co_spawn(
      strand_,
      [this, uri, text]() -> asio::awaitable<void> {
        // Parse the file - ExtractSymbols no longer needed
        co_await ParseFile(uri, text);
      },
      asio::detached);

  co_return;
}

asio::awaitable<void> SlangdLspServer::HandleTextDocumentDidChange(
    const std::optional<nlohmann::json>& params) {
  if (!params) {
    co_return;
  }

  const auto& param_val = params.value();
  const auto& uri = param_val["textDocument"]["uri"].get<std::string>();
  const auto& changes = param_val["contentChanges"];

  // For full sync, we get the full content in the first change
  if (!changes.empty()) {
    const auto& text = changes[0]["text"].get<std::string>();

    // Update the document and re-parse
    auto file_opt = GetOpenFile(uri);
    if (file_opt) {
      lsp::OpenFile& file = file_opt->get();
      file.content = text;
      file.version++;

      // Re-parse the file - ExtractSymbols no longer needed
      asio::co_spawn(
          strand_,
          [this, uri, text]() -> asio::awaitable<void> {
            co_await ParseFile(uri, text);
          },
          asio::detached);
    }
  }

  co_return;
}

asio::awaitable<void> SlangdLspServer::HandleTextDocumentDidClose(
    const std::optional<nlohmann::json>& params) {
  if (!params) {
    co_return;
  }

  const auto& param_val = params.value();
  const auto& uri = param_val["textDocument"]["uri"].get<std::string>();

  // Remove from open files
  RemoveOpenFile(uri);

  co_return;
}

asio::awaitable<nlohmann::json>
SlangdLspServer::HandleTextDocumentDocumentSymbol(
    const std::optional<nlohmann::json>& params) {
  // Default empty result
  nlohmann::json result = nlohmann::json::array();

  if (!params) {
    co_return result;
  }

  // Extract the URI from the parameters
  const auto& param_val = params.value();
  const auto& uri = param_val["textDocument"]["uri"].get<std::string>();

  spdlog::info("Document symbol request for: {}", uri);

  // Get document symbols from document manager
  auto document_symbols = co_await document_manager_->GetDocumentSymbols(uri);

  // Convert to JSON (the document_symbol.hpp should provide conversion
  // functions)
  for (const auto& symbol : document_symbols) {
    result.push_back(symbol);
  }

  co_return result;
}

asio::awaitable<std::expected<void, ParseError>> SlangdLspServer::ParseFile(
    const std::string& uri, const std::string& content) {
  // Forward the DocumentManager's parse result directly
  auto parse_result = co_await document_manager_->ParseDocument(uri, content);

  // If there's an error, log it and notify the client before returning
  if (!parse_result) {
    std::string error_message;
    switch (parse_result.error()) {
      case ParseError::SyntaxError:
        error_message = "Syntax error in SystemVerilog file: " + uri;
        break;
      case ParseError::FileNotFound:
        error_message = "File not found: " + uri;
        break;
      case ParseError::EncodingError:
        error_message = "Text encoding error in file: " + uri;
        break;
      case ParseError::SlangInternalError:
        error_message = "Internal error in slang parser for file: " + uri;
        break;
      case ParseError::UnknownError:
      default:
        error_message = "Unknown error parsing file: " + uri;
        break;
    }

    spdlog::error("Parse error: {}", error_message);

    // Send error message to client as a "window/showMessage" notification
    if (endpoint_) {
      nlohmann::json params = {
          {"type", 1},  // 1 = Error
          {"message", error_message}};

      // Properly co_await the notification instead of ignoring it
      try {
        co_await endpoint_->SendNotification("window/showMessage", params);
      } catch (const std::exception& e) {
        spdlog::error(
            "Failed to send error notification to client: {}", e.what());
      }
    }
  }

  // Return the original result with error information preserved
  co_return parse_result;
}

}  // namespace slangd

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

  endpoint_->RegisterNotification(
      "textDocument/didSave",
      [this](const std::optional<nlohmann::json>& params)
          -> asio::awaitable<void> {
        return HandleTextDocumentDidSave(params);
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
      {"textDocumentSync",
       {{"openClose", true}, {"change", 1}, {"save", true}}},
      {"documentSymbolProvider", true},
      {"publishDiagnostics", {}}};

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

  // Manually extract fields to avoid exceptions
  const auto& json = params.value();
  if (!json.contains("textDocument") || !json["textDocument"].is_object()) {
    spdlog::error("Missing or invalid textDocument field");
    co_return;
  }

  const auto& textDoc = json["textDocument"];

  // Extract fields with fallbacks
  std::string uri = textDoc.value("uri", "");
  std::string text = textDoc.value("text", "");
  std::string language_id = textDoc.value("languageId", "");
  int version = textDoc.value("version", 0);

  // Validate required fields
  if (uri.empty() || text.empty()) {
    spdlog::error("Missing required fields in textDocument");
    co_return;
  }

  // Store the document
  AddOpenFile(uri, text, language_id, version);

  // Post to strand to ensure thread safety
  asio::co_spawn(
      strand_,
      [this, uri, text, version]() -> asio::awaitable<void> {
        // Parse with basic compilation for initial open
        auto parse_result =
            co_await document_manager_->ParseWithBasicCompilation(uri, text);

        if (!parse_result) {
          spdlog::debug(
              "Parse error on document open: {} - {}", uri,
              static_cast<int>(parse_result.error()));
        }

        // Get and publish diagnostics (even on error, to show available
        // diagnostics)
        auto diagnostics =
            co_await document_manager_->GetDocumentDiagnostics(uri);
        lsp::PublishDiagnosticsParams params{uri, version, diagnostics};
        co_await PublishDiagnostics(params);
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
  int version = param_val["textDocument"].contains("version")
                    ? param_val["textDocument"]["version"].get<int>()
                    : 0;

  // For full sync, we get the full content in the first change
  if (!changes.empty()) {
    const auto& text = changes[0]["text"].get<std::string>();

    // Update the document
    auto file_opt = GetOpenFile(uri);
    if (file_opt) {
      lsp::OpenFile& file = file_opt->get();
      file.content = text;
      file.version++;

      // Re-parse the file using syntax-only parsing (fast)
      asio::co_spawn(
          strand_,
          [this, uri, text, version]() -> asio::awaitable<void> {
            // Parse with syntax only for fast feedback during typing
            auto parse_result =
                co_await document_manager_->ParseSyntaxOnly(uri, text);

            if (!parse_result) {
              spdlog::debug(
                  "Parse error on document change: {} - {}", uri,
                  static_cast<int>(parse_result.error()));
            }

            // Get and publish diagnostics (even on error, to show available
            // diagnostics)
            auto diagnostics =
                co_await document_manager_->GetDocumentDiagnostics(uri);

            lsp::PublishDiagnosticsParams params{uri, version, diagnostics};
            co_await PublishDiagnostics(params);
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

asio::awaitable<void> SlangdLspServer::HandleTextDocumentDidSave(
    const std::optional<nlohmann::json>& params) {
  if (!params) {
    co_return;
  }

  const auto& param_val = params.value();
  const auto& uri = param_val["textDocument"]["uri"].get<std::string>();
  int version = param_val["textDocument"].contains("version")
                    ? param_val["textDocument"]["version"].get<int>()
                    : 0;

  // Get the current content from open files
  auto file_opt = GetOpenFile(uri);
  if (!file_opt) {
    co_return;
  }

  const std::string& text = file_opt->get().content;

  // Post to strand to ensure thread safety
  asio::co_spawn(
      strand_,
      [this, uri, text, version]() -> asio::awaitable<void> {
        // Parse with basic compilation on save for more comprehensive analysis
        auto parse_result =
            co_await document_manager_->ParseWithBasicCompilation(uri, text);

        if (!parse_result) {
          spdlog::debug(
              "Parse error on document save: {} - {}", uri,
              static_cast<int>(parse_result.error()));
        }

        // Get and publish diagnostics (even on error, to show available
        // diagnostics)
        auto diagnostics =
            co_await document_manager_->GetDocumentDiagnostics(uri);

        lsp::PublishDiagnosticsParams params{uri, version, diagnostics};
        co_await PublishDiagnostics(params);
      },
      asio::detached);

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

  spdlog::info("Document symbol request for {}", uri);

  // Get document symbols from document manager
  // Use the strand to ensure thread safety when accessing document symbols
  auto document_symbols = co_await asio::co_spawn(
      strand_,
      [this, uri]() -> asio::awaitable<std::vector<lsp::DocumentSymbol>> {
        return document_manager_->GetDocumentSymbols(uri);
      },
      asio::use_awaitable);

  // Convert to JSON (the document_symbol.hpp should provide conversion
  // functions)
  for (const auto& symbol : document_symbols) {
    result.push_back(symbol);
  }

  co_return result;
}

}  // namespace slangd

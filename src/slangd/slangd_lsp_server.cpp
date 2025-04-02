#include "slangd/slangd_lsp_server.hpp"

#include <slang/syntax/AllSyntax.h>
#include <slang/syntax/SyntaxVisitor.h>
#include <spdlog/spdlog.h>

#include "lsp/document_features.hpp"

namespace slangd {

SlangdLspServer::SlangdLspServer(
    asio::any_io_executor executor,
    std::unique_ptr<jsonrpc::endpoint::RpcEndpoint> endpoint)
    : lsp::LspServer(executor, std::move(endpoint)),
      strand_(asio::make_strand(executor)),
      document_manager_(std::make_unique<DocumentManager>(executor)) {}

auto SlangdLspServer::OnInitialize(const lsp::InitializeParams& params)
    -> asio::awaitable<lsp::InitializeResult> {
  spdlog::info("SlangdLspServer OnInitialize");
  lsp::TextDocumentSyncOptions syncOptions{
      .openClose = true,
      .change = lsp::TextDocumentSyncKind::Full,
  };

  lsp::ServerCapabilities capabilities{
      .textDocumentSync = syncOptions,
      .documentSymbolProvider = true,
  };

  co_return lsp::InitializeResult{
      .capabilities = capabilities,
      .serverInfo = lsp::InitializeResult::ServerInfo{"slangd", "0.1.0"}};
}

auto SlangdLspServer::OnInitialized(const lsp::InitializedParams& params)
    -> asio::awaitable<void> {
  spdlog::info("SlangdLspServer OnInitialized");
  initialized_ = true;
  co_return;
}

auto SlangdLspServer::OnShutdown(const lsp::ShutdownParams& params)
    -> asio::awaitable<lsp::ShutdownResult> {
  shutdown_requested_ = true;
  spdlog::info("SlangdLspServer OnShutdown");
  co_return lsp::ShutdownResult{};
}

auto SlangdLspServer::OnExit(const lsp::ExitParams& params)
    -> asio::awaitable<void> {
  spdlog::info("SlangdLspServer OnExit");
  co_await lsp::LspServer::Shutdown();
  co_return;
}

asio::awaitable<void> SlangdLspServer::OnDidOpenTextDocument(
    const lsp::DidOpenTextDocumentParams& params) {
  spdlog::info("SlangdLspServer OnDidOpenTextDocument");
  const auto& textDoc = params.textDocument;
  const auto& uri = textDoc.uri;
  const auto& text = textDoc.text;
  const auto& languageId = textDoc.languageId;
  const auto& version = textDoc.version;

  AddOpenFile(uri, text, languageId, version);

  // Post to strand to ensure thread safety
  asio::co_spawn(
      strand_,
      [this, uri, text, version]() -> asio::awaitable<void> {
        // Parse with compilation for initial open
        auto parse_result =
            co_await document_manager_->ParseWithCompilation(uri, text);

        if (!parse_result) {
          spdlog::debug(
              "Parse error on document open: {} - {}", uri,
              static_cast<int>(parse_result.error()));
        }

        // Get and publish diagnostics (even for empty files)
        auto diagnostics =
            co_await document_manager_->GetDocumentDiagnostics(uri);
        lsp::PublishDiagnosticsParams params{uri, version, diagnostics};
        co_await PublishDiagnostics(params);
      },
      asio::detached);

  co_return;
}

asio::awaitable<void> SlangdLspServer::OnDidChangeTextDocument(
    const lsp::DidChangeTextDocumentParams& params) {
  spdlog::info("SlangdLspServer OnDidChangeTextDocument");

  const auto& textDoc = params.textDocument;
  const auto& uri = textDoc.uri;
  const auto& changes = params.contentChanges;

  int version = textDoc.version;

  // For full sync, we get the full content in the first change
  if (!changes.empty()) {
    const auto& full_change =
        std::get<lsp::TextDocumentContentFullChangeEvent>(changes[0]);
    const auto& text = full_change.text;

    auto file_opt = GetOpenFile(uri);
    if (file_opt) {
      lsp::OpenFile& file = file_opt->get();
      file.content = text;
      file.version = version;

      // Re-parse the file using syntax-only parsing (fast)
      asio::co_spawn(
          strand_,
          [this, uri, text, version]() -> asio::awaitable<void> {
            // Parse with compilation for interactive feedback
            auto parse_result =
                co_await document_manager_->ParseWithCompilation(uri, text);

            if (!parse_result) {
              spdlog::debug(
                  "Parse error on document change: {} - {}", uri,
                  static_cast<int>(parse_result.error()));
            }

            // Get and publish diagnostics
            auto diagnostics =
                co_await document_manager_->GetDocumentDiagnostics(uri);

            co_await PublishDiagnostics(
                lsp::PublishDiagnosticsParams{uri, version, diagnostics});
          },
          asio::detached);
    }
  }

  co_return;
}

asio::awaitable<void> SlangdLspServer::OnDidCloseTextDocument(
    const lsp::DidCloseTextDocumentParams& params) {
  spdlog::info("SlangdLspServer OnDidCloseTextDocument");

  const auto& uri = params.textDocument.uri;

  RemoveOpenFile(uri);

  co_return;
}

asio::awaitable<lsp::DocumentSymbolResult> SlangdLspServer::OnDocumentSymbols(
    const lsp::DocumentSymbolParams& params) {
  spdlog::info("SlangdLspServer OnDocumentSymbols");
  std::vector<lsp::DocumentSymbol> result;

  const auto& uri = params.textDocument.uri;

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

  spdlog::info("SlangdLspServer OnDocumentSymbols result: {}", result.size());

  co_return result;
}

}  // namespace slangd

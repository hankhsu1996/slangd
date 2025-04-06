#include "slangd/slangd_lsp_server.hpp"

#include <slang/syntax/AllSyntax.h>
#include <slang/syntax/SyntaxVisitor.h>
#include <spdlog/spdlog.h>

#include "lsp/document_features.hpp"

namespace slangd {

using lsp::LspError;
using lsp::LspErrorCode;
using lsp::Ok;

SlangdLspServer::SlangdLspServer(
    asio::any_io_executor executor,
    std::unique_ptr<jsonrpc::endpoint::RpcEndpoint> endpoint)
    : lsp::LspServer(executor, std::move(endpoint)),
      strand_(asio::make_strand(executor)),
      document_manager_(std::make_unique<DocumentManager>(executor)),
      workspace_manager_(std::make_unique<WorkspaceManager>(executor)) {
}

auto SlangdLspServer::OnInitialize(lsp::InitializeParams params)
    -> asio::awaitable<std::expected<lsp::InitializeResult, lsp::LspError>> {
  spdlog::debug("SlangdLspServer OnInitialize");

  if (auto workspace_folders_opt = params.workspaceFolders) {
    for (const auto& folder : *workspace_folders_opt) {
      workspace_manager_->AddWorkspaceFolder(folder.uri, folder.name);
    }
  }

  lsp::TextDocumentSyncOptions sync_options{
      .openClose = true,
      .change = lsp::TextDocumentSyncKind::Full,
  };

  lsp::ServerCapabilities::Workspace workspace{
      .workspaceFolders =
          lsp::WorkspaceFoldersServerCapabilities{
              .supported = true,
          },
  };

  lsp::ServerCapabilities capabilities{
      .textDocumentSync = sync_options,
      .documentSymbolProvider = true,
      .workspace = workspace,
  };

  co_return lsp::InitializeResult{
      .capabilities = capabilities,
      .serverInfo = lsp::InitializeResult::ServerInfo{
          .name = "slangd", .version = "0.1.0"}};
}

auto SlangdLspServer::OnInitialized(lsp::InitializedParams /*unused*/)
    -> asio::awaitable<std::expected<void, lsp::LspError>> {
  spdlog::debug("SlangdLspServer OnInitialized");
  initialized_ = true;

  auto scan_workspace = [this]() -> asio::awaitable<void> {
    spdlog::debug(
        "SlangdLspServer starting workspace scan for SystemVerilog files");
    co_await workspace_manager_->ScanWorkspace();
    spdlog::debug("SlangdLspServer workspace scan completed");
  };

  // Start workspace scanning in the background
  asio::co_spawn(strand_, scan_workspace, asio::detached);

  co_return Ok();
}

auto SlangdLspServer::OnShutdown(lsp::ShutdownParams /*unused*/)
    -> asio::awaitable<std::expected<lsp::ShutdownResult, lsp::LspError>> {
  shutdown_requested_ = true;
  spdlog::debug("SlangdLspServer OnShutdown");
  co_return lsp::ShutdownResult{};
}

auto SlangdLspServer::OnExit(lsp::ExitParams /*unused*/)
    -> asio::awaitable<std::expected<void, lsp::LspError>> {
  spdlog::debug("SlangdLspServer OnExit");
  co_await lsp::LspServer::Shutdown();
  co_return Ok();
}

auto SlangdLspServer::OnDidOpenTextDocument(
    lsp::DidOpenTextDocumentParams params)
    -> asio::awaitable<std::expected<void, lsp::LspError>> {
  spdlog::debug("SlangdLspServer OnDidOpenTextDocument");
  const auto& text_doc = params.textDocument;
  const auto& uri = text_doc.uri;
  const auto& text = text_doc.text;
  const auto& language_id = text_doc.languageId;
  const auto& version = text_doc.version;

  AddOpenFile(uri, text, language_id, version);

  // Post to strand to ensure thread safety and sequencing
  asio::co_spawn(
      strand_,
      [this, uri, text, version]() -> asio::awaitable<void> {
        spdlog::debug("SlangdLspServer starting document parse for: {}", uri);

        // Parse with compilation for initial open
        auto parse_result =
            co_await document_manager_->ParseWithCompilation(uri, text);

        if (!parse_result) {
          spdlog::debug(
              "SlangdLspServer parse error on document open: {} - {}", uri,
              static_cast<int>(parse_result.error()));
        }

        // Get diagnostics (even for empty files)
        auto diagnostics =
            co_await document_manager_->GetDocumentDiagnostics(uri);

        spdlog::debug(
            "SlangdLspServer publishing {} diagnostics for document: {}",
            diagnostics.size(), uri);

        // Publish diagnostics only once with all collected diagnostics
        lsp::PublishDiagnosticsParams diag_params{
            .uri = uri, .version = version, .diagnostics = diagnostics};
        co_await PublishDiagnostics(diag_params);

        spdlog::debug(
            "SlangdLspServer completed publishing diagnostics for: {}", uri);
      },
      asio::detached);

  co_return Ok();
}

auto SlangdLspServer::OnDidChangeTextDocument(
    lsp::DidChangeTextDocumentParams params)
    -> asio::awaitable<std::expected<void, lsp::LspError>> {
  spdlog::debug("SlangdLspServer OnDidChangeTextDocument");

  const auto& text_doc = params.textDocument;
  const auto& uri = text_doc.uri;
  const auto& changes = params.contentChanges;

  int version = text_doc.version;

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
            spdlog::debug("Starting document change parse for: {}", uri);

            // Parse with compilation for interactive feedback
            auto parse_result =
                co_await document_manager_->ParseWithCompilation(uri, text);

            if (!parse_result) {
              spdlog::debug(
                  "Parse error on document change: {} - {}", uri,
                  static_cast<int>(parse_result.error()));
            }

            // Get diagnostics
            auto diagnostics =
                co_await document_manager_->GetDocumentDiagnostics(uri);

            spdlog::debug(
                "Publishing {} diagnostics for document change: {}",
                diagnostics.size(), uri);

            // Publish diagnostics as a single batch
            lsp::PublishDiagnosticsParams diag_params{
                .uri = uri, .version = version, .diagnostics = diagnostics};
            co_await PublishDiagnostics(diag_params);

            spdlog::debug(
                "Completed publishing diagnostics for change: {}", uri);
          },
          asio::detached);
    }
  }

  co_return Ok();
}

auto SlangdLspServer::OnDidCloseTextDocument(
    lsp::DidCloseTextDocumentParams params)
    -> asio::awaitable<std::expected<void, lsp::LspError>> {
  spdlog::debug("SlangdLspServer OnDidCloseTextDocument");

  const auto& uri = params.textDocument.uri;

  RemoveOpenFile(uri);

  co_return Ok();
}

auto SlangdLspServer::OnDocumentSymbols(lsp::DocumentSymbolParams params)
    -> asio::awaitable<
        std::expected<lsp::DocumentSymbolResult, lsp::LspError>> {
  spdlog::debug("SlangdLspServer OnDocumentSymbols");
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

  spdlog::debug("SlangdLspServer OnDocumentSymbols result: {}", result.size());

  co_return result;
}

}  // namespace slangd

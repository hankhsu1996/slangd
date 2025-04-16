#include "slangd/core/slangd_lsp_server.hpp"

#include <lsp/registeration_options.hpp>
#include <slang/syntax/AllSyntax.h>
#include <slang/syntax/SyntaxVisitor.h>
#include <spdlog/spdlog.h>

#include "lsp/document_features.hpp"

namespace slangd {

using lsp::LspError;
using lsp::Ok;

SlangdLspServer::SlangdLspServer(
    asio::any_io_executor executor,
    std::unique_ptr<jsonrpc::endpoint::RpcEndpoint> endpoint,
    std::shared_ptr<spdlog::logger> logger)
    : lsp::LspServer(executor, std::move(endpoint), logger),
      strand_(asio::make_strand(executor)),
      document_manager_(std::make_shared<DocumentManager>(executor, logger)),
      workspace_manager_(std::make_shared<WorkspaceManager>(executor, logger)),
      definition_provider_(std::make_unique<DefinitionProvider>(
          document_manager_, workspace_manager_, logger)),
      diagnostics_provider_(std::make_unique<DiagnosticsProvider>(
          document_manager_, workspace_manager_, logger)),
      symbols_provider_(std::make_unique<SymbolsProvider>(
          document_manager_, workspace_manager_, logger)) {
}

auto SlangdLspServer::OnInitialize(lsp::InitializeParams params)
    -> asio::awaitable<std::expected<lsp::InitializeResult, lsp::LspError>> {
  Logger()->debug("SlangdLspServer OnInitialize");

  if (auto workspace_folders_opt = params.workspaceFolders) {
    for (const auto& folder : *workspace_folders_opt) {
      workspace_manager_->AddWorkspaceFolder(folder.uri, folder.name);
    }
  }

  lsp::TextDocumentSyncOptions sync_options{
      .openClose = true,
      .change = lsp::TextDocumentSyncKind::kFull,
  };

  lsp::ServerCapabilities::Workspace workspace{
      .workspaceFolders =
          lsp::WorkspaceFoldersServerCapabilities{
              .supported = true,
          },
  };

  lsp::ServerCapabilities capabilities{
      .textDocumentSync = sync_options,
      .definitionProvider = true,
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
  Logger()->debug("SlangdLspServer OnInitialized");
  initialized_ = true;

  // Scan workspace for SystemVerilog files to build the initial index
  auto scan_workspace = [this]() -> asio::awaitable<void> {
    Logger()->debug(
        "SlangdLspServer starting workspace scan for SystemVerilog files");
    co_await workspace_manager_->ScanWorkspace();
    Logger()->debug("SlangdLspServer workspace scan completed");
  };

  // Register the file system watcher for workspace changes
  auto register_watcher = [this]() -> asio::awaitable<void> {
    Logger()->debug("SlangdLspServer registering file system watcher");
    auto watcher = lsp::FileSystemWatcher{.globPattern = "**/*.{sv,svh,v,vh}"};
    auto options = lsp::DidChangeWatchedFilesRegistrationOptions{
        .watchers = {watcher},
    };
    auto registration = lsp::Registration{
        .id = "slangd-file-system-watcher",
        .method = "workspace/didChangeWatchedFiles",
        .registerOptions = options,
    };
    auto params = lsp::RegistrationParams{
        .registrations = {registration},
    };
    auto result = co_await RegisterCapability(params);
    if (!result) {
      Logger()->error(
          "SlangdLspServer failed to register file system watcher: {}",
          result.error().Message());
      co_return;
    }
    Logger()->debug("SlangdLspServer file system watcher registered");
    co_return;
  };

  // Start workspace scanning in the background
  asio::co_spawn(strand_, scan_workspace, asio::detached);
  asio::co_spawn(strand_, register_watcher, asio::detached);

  co_return Ok();
}

auto SlangdLspServer::OnShutdown(lsp::ShutdownParams /*unused*/)
    -> asio::awaitable<std::expected<lsp::ShutdownResult, lsp::LspError>> {
  shutdown_requested_ = true;
  Logger()->debug("SlangdLspServer OnShutdown");
  co_return lsp::ShutdownResult{};
}

auto SlangdLspServer::OnExit(lsp::ExitParams /*unused*/)
    -> asio::awaitable<std::expected<void, lsp::LspError>> {
  Logger()->debug("SlangdLspServer OnExit");
  co_await lsp::LspServer::Shutdown();
  co_return Ok();
}

auto SlangdLspServer::OnDidOpenTextDocument(
    lsp::DidOpenTextDocumentParams params)
    -> asio::awaitable<std::expected<void, lsp::LspError>> {
  Logger()->debug("SlangdLspServer OnDidOpenTextDocument");
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
        Logger()->debug("SlangdLspServer starting document parse for: {}", uri);

        // Parse with compilation for initial open
        co_await document_manager_->ParseWithCompilation(uri, text);

        // Get diagnostics (even for empty files)
        auto diagnostics = diagnostics_provider_->GetDiagnosticsForUri(uri);

        Logger()->debug(
            "SlangdLspServer publishing {} diagnostics for document: {}",
            diagnostics.size(), uri);

        // Publish diagnostics only once with all collected diagnostics
        lsp::PublishDiagnosticsParams diag_params{
            .uri = uri, .version = version, .diagnostics = diagnostics};
        co_await PublishDiagnostics(diag_params);

        Logger()->debug(
            "SlangdLspServer completed publishing diagnostics for: {}", uri);
      },
      asio::detached);

  co_return Ok();
}

auto SlangdLspServer::OnDidChangeTextDocument(
    lsp::DidChangeTextDocumentParams params)
    -> asio::awaitable<std::expected<void, lsp::LspError>> {
  Logger()->debug("SlangdLspServer OnDidChangeTextDocument");

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
            Logger()->debug("Starting document change parse for: {}", uri);

            // Parse with compilation for interactive feedback
            co_await document_manager_->ParseWithCompilation(uri, text);

            // Get diagnostics
            auto diagnostics = diagnostics_provider_->GetDiagnosticsForUri(uri);

            Logger()->debug(
                "Publishing {} diagnostics for document change: {}",
                diagnostics.size(), uri);

            // Publish diagnostics as a single batch
            lsp::PublishDiagnosticsParams diag_params{
                .uri = uri, .version = version, .diagnostics = diagnostics};
            co_await PublishDiagnostics(diag_params);

            Logger()->debug(
                "Completed publishing diagnostics for change: {}", uri);
          },
          asio::detached);
    }
  }

  co_return Ok();
}

auto SlangdLspServer::OnDidSaveTextDocument(
    lsp::DidSaveTextDocumentParams params)
    -> asio::awaitable<std::expected<void, lsp::LspError>> {
  Logger()->debug("SlangdLspServer OnDidSaveTextDocument");

  const auto& text_doc = params.textDocument;
  const auto& uri = text_doc.uri;

  // We reparse with full elaboration
  // Need the sync to ensure the file is parsed before we get the diagnostics
  asio::co_spawn(
      strand_,
      [this, uri]() -> asio::awaitable<void> {
        auto file_opt = GetOpenFile(uri);
        if (file_opt) {
          lsp::OpenFile& file = file_opt->get();
          co_await document_manager_->ParseWithElaboration(uri, file.content);
        }
      },
      asio::detached);

  co_return Ok();
}

auto SlangdLspServer::OnDidCloseTextDocument(
    lsp::DidCloseTextDocumentParams params)
    -> asio::awaitable<std::expected<void, lsp::LspError>> {
  Logger()->debug("SlangdLspServer OnDidCloseTextDocument");

  const auto& uri = params.textDocument.uri;

  RemoveOpenFile(uri);

  co_return Ok();
}

auto SlangdLspServer::OnDocumentSymbols(lsp::DocumentSymbolParams params)
    -> asio::awaitable<
        std::expected<lsp::DocumentSymbolResult, lsp::LspError>> {
  Logger()->debug("SlangdLspServer OnDocumentSymbols");

  co_await asio::post(strand_, asio::use_awaitable);
  co_return symbols_provider_->GetSymbolsForUri(params.textDocument.uri);
}

auto SlangdLspServer::OnGotoDefinition(lsp::DefinitionParams params)
    -> asio::awaitable<std::expected<lsp::DefinitionResult, lsp::LspError>> {
  Logger()->debug("SlangdLspServer OnGotoDefinition");

  co_return definition_provider_->GetDefinitionForUri(
      std::string(params.textDocument.uri), params.position);
}

auto SlangdLspServer::OnDidChangeWatchedFiles(
    lsp::DidChangeWatchedFilesParams params)
    -> asio::awaitable<std::expected<void, lsp::LspError>> {
  Logger()->debug("SlangdLspServer OnDidChangeWatchedFiles");

  asio::co_spawn(
      strand_,
      [this, params]() -> asio::awaitable<void> {
        co_await workspace_manager_->HandleFileChanges(params.changes);
      },
      asio::detached);

  co_return Ok();
}

}  // namespace slangd

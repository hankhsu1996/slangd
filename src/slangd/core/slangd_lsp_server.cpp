#include "slangd/core/slangd_lsp_server.hpp"

#include <lsp/registeration_options.hpp>
#include <slang/syntax/AllSyntax.h>
#include <slang/syntax/SyntaxVisitor.h>
#include <spdlog/spdlog.h>

#include "lsp/document_features.hpp"
#include "slangd/utils/canonical_path.hpp"
#include "slangd/utils/path_utils.hpp"

namespace slangd {

using lsp::LspError;
using lsp::LspErrorCode;
using lsp::Ok;

namespace {
constexpr std::string_view kServerVersion = "0.1.0";
constexpr std::string_view kFileWatcherId = "slangd-file-system-watcher";
constexpr std::string_view kDidChangeWatchedFilesMethod =
    "workspace/didChangeWatchedFiles";
}  // namespace

SlangdLspServer::SlangdLspServer(
    asio::any_io_executor executor,
    std::unique_ptr<jsonrpc::endpoint::RpcEndpoint> endpoint,
    std::shared_ptr<LanguageServiceBase> language_service,
    std::shared_ptr<spdlog::logger> logger)
    : lsp::LspServer(executor, std::move(endpoint), logger),
      logger_(logger ? logger : spdlog::default_logger()),
      executor_(executor),
      language_service_(std::move(language_service)) {
}

auto SlangdLspServer::OnInitialize(lsp::InitializeParams params)
    -> asio::awaitable<std::expected<lsp::InitializeResult, lsp::LspError>> {
  if (const auto& workspace_folders_opt = params.workspaceFolders) {
    if (workspace_folders_opt->size() != 1) {
      co_return LspError::UnexpectedFromCode(
          LspErrorCode::kInvalidRequest, "Only one workspace is supported");
    }

    co_await language_service_->InitializeWorkspace(
        workspace_folders_opt->front().uri);
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
          .name = "slangd", .version = std::string(kServerVersion)}};
}

auto SlangdLspServer::OnInitialized(lsp::InitializedParams /*unused*/)
    -> asio::awaitable<std::expected<void, lsp::LspError>> {
  initialized_ = true;

  auto register_watcher = [this]() -> asio::awaitable<void> {
    lsp::FileSystemWatcher sv_files{.globPattern = "**/*.{sv,svh,v,vh}"};
    lsp::FileSystemWatcher slangd_config{.globPattern = "**/.slangd"};
    auto registration = lsp::Registration{
        .id = std::string(kFileWatcherId),
        .method = std::string(kDidChangeWatchedFilesMethod),
        .registerOptions =
            lsp::DidChangeWatchedFilesRegistrationOptions{
                .watchers = {sv_files, slangd_config},
            },
    };
    auto result = co_await RegisterCapability(
        lsp::RegistrationParams{.registrations = {registration}});
    if (!result) {
      Logger()->error(
          "Failed to register file system watcher: {}",
          result.error().Message());
    }
    co_return;
  };

  asio::co_spawn(executor_, register_watcher, asio::detached);
  co_return Ok();
}

auto SlangdLspServer::OnShutdown(lsp::ShutdownParams /*unused*/)
    -> asio::awaitable<std::expected<lsp::ShutdownResult, lsp::LspError>> {
  shutdown_requested_ = true;
  co_return lsp::ShutdownResult{};
}

auto SlangdLspServer::OnExit(lsp::ExitParams /*unused*/)
    -> asio::awaitable<std::expected<void, lsp::LspError>> {
  co_await lsp::LspServer::Shutdown();
  co_return Ok();
}

auto SlangdLspServer::OnDidOpenTextDocument(
    lsp::DidOpenTextDocumentParams params)
    -> asio::awaitable<std::expected<void, lsp::LspError>> {
  const auto& text_doc = params.textDocument;
  Logger()->debug("OnDidOpenTextDocument received: {}", text_doc.uri);

  co_await language_service_->OnDocumentOpened(
      text_doc.uri, text_doc.text, text_doc.version);

  asio::co_spawn(
      executor_,
      [this, uri = text_doc.uri, text = text_doc.text,
       version = text_doc.version]() -> asio::awaitable<void> {
        co_await PublishDiagnosticsForDocument(uri, text, version);
      },
      asio::detached);

  co_return Ok();
}

auto SlangdLspServer::OnDidChangeTextDocument(
    lsp::DidChangeTextDocumentParams params)
    -> asio::awaitable<std::expected<void, lsp::LspError>> {
  Logger()->debug(
      "OnDidChangeTextDocument received: {}", params.textDocument.uri);
  if (!params.contentChanges.empty()) {
    const auto& full_change = std::get<lsp::TextDocumentContentFullChangeEvent>(
        params.contentChanges[0]);
    co_await language_service_->OnDocumentChanged(
        params.textDocument.uri, full_change.text, params.textDocument.version);
  }
  co_return Ok();
}

auto SlangdLspServer::OnDidSaveTextDocument(
    lsp::DidSaveTextDocumentParams params)
    -> asio::awaitable<std::expected<void, lsp::LspError>> {
  Logger()->debug(
      "OnDidSaveTextDocument received: {}", params.textDocument.uri);
  co_await language_service_->OnDocumentSaved(params.textDocument.uri);
  co_await ProcessDiagnosticsForUri(params.textDocument.uri);
  co_return Ok();
}

auto SlangdLspServer::OnDidCloseTextDocument(
    lsp::DidCloseTextDocumentParams params)
    -> asio::awaitable<std::expected<void, lsp::LspError>> {
  Logger()->debug(
      "OnDidCloseTextDocument received: {}", params.textDocument.uri);
  language_service_->OnDocumentClosed(params.textDocument.uri);
  co_return Ok();
}

auto SlangdLspServer::PublishDiagnosticsForDocument(
    std::string uri, std::string content, int version)
    -> asio::awaitable<void> {
  auto all_diags_result = co_await language_service_->ComputeDiagnostics(uri);
  if (all_diags_result.has_value()) {
    co_await PublishDiagnostics(
        {.uri = uri, .version = version, .diagnostics = *all_diags_result});
  }
}

auto SlangdLspServer::ProcessDiagnosticsForUri(std::string uri)
    -> asio::awaitable<void> {
  auto doc_state = co_await language_service_->GetDocumentState(uri);
  if (doc_state) {
    co_await PublishDiagnosticsForDocument(
        uri, doc_state->content, doc_state->version);
  }
}

auto SlangdLspServer::OnDocumentSymbols(lsp::DocumentSymbolParams params)
    -> asio::awaitable<
        std::expected<lsp::DocumentSymbolResult, lsp::LspError>> {
  Logger()->debug("OnDocumentSymbols received: {}", params.textDocument.uri);
  co_return co_await language_service_->GetDocumentSymbols(
      params.textDocument.uri);
}

auto SlangdLspServer::OnGotoDefinition(lsp::DefinitionParams params)
    -> asio::awaitable<std::expected<lsp::DefinitionResult, lsp::LspError>> {
  Logger()->debug("OnGotoDefinition received: {}", params.textDocument.uri);
  co_return co_await language_service_->GetDefinitionsForPosition(
      params.textDocument.uri, params.position);
}

auto SlangdLspServer::OnDidChangeWatchedFiles(
    lsp::DidChangeWatchedFilesParams params)
    -> asio::awaitable<std::expected<void, lsp::LspError>> {
  asio::co_spawn(
      executor_,
      [this, params]() -> asio::awaitable<void> {
        bool has_config_change = false;

        // Single pass: Process each change once
        for (const auto& change : params.changes) {
          if (IsConfigFile(change.uri)) {
            has_config_change = true;
            continue;
          }

          auto path = CanonicalPath::FromUri(change.uri);
          if (IsSystemVerilogFile(path.Path())) {
            // Ignore open files - managed by LSP text sync
            // (VSCode prompts "reload?" and sends didChange with new version)
            if (co_await language_service_->GetDocumentState(change.uri)) {
              continue;
            }

            // Closed file changed - invalidate sessions
            // (Any file might be package/interface included in all sessions)
            co_await language_service_->HandleSourceFileChange(
                change.uri, change.type);
          }
        }

        // Config changes affect everything (search paths, macros, etc.)
        if (has_config_change) {
          co_await language_service_->HandleConfigChange();

          // Republish diagnostics for all open files after rebuild
          auto open_uris = co_await language_service_->GetAllOpenDocumentUris();
          for (const auto& uri : open_uris) {
            co_await ProcessDiagnosticsForUri(uri);
          }
        }

        co_return;
      },
      asio::detached);

  co_return Ok();
}

auto SlangdLspServer::IsConfigFile(const std::string& path) -> bool {
  return path.ends_with("/.slangd") || path.ends_with("\\.slangd");
}

}  // namespace slangd

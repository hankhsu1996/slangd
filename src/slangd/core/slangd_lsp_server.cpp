#include "slangd/core/slangd_lsp_server.hpp"

#include <lsp/registeration_options.hpp>
#include <slang/syntax/AllSyntax.h>
#include <slang/syntax/SyntaxVisitor.h>
#include <spdlog/spdlog.h>

#include "lsp/document_features.hpp"
#include "slangd/services/legacy/legacy_language_service.hpp"

namespace slangd {

using lsp::LspError;
using lsp::LspErrorCode;
using lsp::Ok;

SlangdLspServer::SlangdLspServer(
    asio::any_io_executor executor,
    std::unique_ptr<jsonrpc::endpoint::RpcEndpoint> endpoint,
    std::shared_ptr<LanguageServiceBase> language_service,
    std::shared_ptr<spdlog::logger> logger)
    : lsp::LspServer(executor, std::move(endpoint), logger),
      logger_(logger ? logger : spdlog::default_logger()),
      executor_(executor),
      strand_(asio::make_strand(executor)),
      language_service_(std::move(language_service)) {
}

auto SlangdLspServer::OnInitialize(lsp::InitializeParams params)
    -> asio::awaitable<std::expected<lsp::InitializeResult, lsp::LspError>> {
  Logger()->debug("SlangdLspServer OnInitialize");

  // Validate workspace folders and initialize language service
  if (const auto& workspace_folders_opt = params.workspaceFolders) {
    if (workspace_folders_opt->size() != 1) {
      Logger()->error("SlangdLspServer only supports a single workspace");
      co_return LspError::UnexpectedFromCode(
          LspErrorCode::kInvalidRequest, "Only one workspace is supported");
    }

    const auto& workspace_folder = workspace_folders_opt->front();

    // Initialize language service with workspace - need to cast to
    // LegacyLanguageService to access InitializeWorkspace
    if (auto legacy_service =
            std::dynamic_pointer_cast<slangd::LegacyLanguageService>(
                language_service_)) {
      co_await legacy_service->InitializeWorkspace(workspace_folder.uri);
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

  // Register file system watcher for workspace changes
  auto register_watcher = [this]() -> asio::awaitable<void> {
    Logger()->debug("SlangdLspServer registering file system watcher");
    lsp::FileSystemWatcher sv_files{.globPattern = "**/*.{sv,svh,v,vh}"};
    lsp::FileSystemWatcher slangd_config{.globPattern = "**/.slangd"};
    auto options = lsp::DidChangeWatchedFilesRegistrationOptions{
        .watchers = {sv_files, slangd_config},
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

  // Register file watcher in background - language service handles workspace
  // scanning
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

  // Compute and publish initial diagnostics via facade
  asio::co_spawn(
      strand_,
      [this, uri, text, version]() -> asio::awaitable<void> {
        Logger()->debug("SlangdLspServer starting document parse for: {}", uri);

        // Use facade to compute diagnostics
        auto diagnostics =
            co_await language_service_->ComputeDiagnostics(uri, text);

        Logger()->debug(
            "SlangdLspServer publishing {} diagnostics for document: {}",
            diagnostics.size(), uri);

        // Publish diagnostics
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

      // Schedule diagnostics with debouncing (moved from DiagnosticsProvider)
      ScheduleDiagnosticsWithDebounce(uri, text, version);
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

  // Process diagnostics immediately on save (no debounce)
  co_await ProcessDiagnosticsForUri(uri);

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
  co_return language_service_->GetDocumentSymbols(params.textDocument.uri);
}

auto SlangdLspServer::OnGotoDefinition(lsp::DefinitionParams params)
    -> asio::awaitable<std::expected<lsp::DefinitionResult, lsp::LspError>> {
  Logger()->debug("SlangdLspServer OnGotoDefinition");

  co_return language_service_->GetDefinitionsForPosition(
      std::string(params.textDocument.uri), params.position);
}

auto SlangdLspServer::OnDidChangeWatchedFiles(
    lsp::DidChangeWatchedFilesParams params)
    -> asio::awaitable<std::expected<void, lsp::LspError>> {
  Logger()->debug("SlangdLspServer OnDidChangeWatchedFiles");

  asio::co_spawn(
      strand_,
      [this, params]() -> asio::awaitable<void> {
        // Process each file change
        for (const auto& change : params.changes) {
          auto path = CanonicalPath::FromUri(change.uri);
          Logger()->debug("SlangdLspServer detected file change: {}", path);

          // Check if this is a config file change and trigger rebuild
          if (IsConfigFile(change.uri)) {
            language_service_->HandleConfigChange();
          }
        }
        co_return;
      },
      asio::detached);

  co_return Ok();
}

// Diagnostics orchestration methods (moved from DiagnosticsProvider)

void SlangdLspServer::ScheduleDiagnosticsWithDebounce(
    std::string uri, std::string text, int version) {
  // Store the request or update existing one
  auto& request = pending_diagnostics_[uri];
  request.text = text;
  request.version = version;

  // Cancel existing timer if there is one
  if (request.timer) {
    request.timer->cancel();
  }

  // Create a new timer with debounce delay
  request.timer =
      std::make_unique<asio::steady_timer>(strand_, debounce_delay_);

  // Set up timer callback
  request.timer->async_wait([this, uri](const asio::error_code& ec) {
    if (ec) {
      return;  // Timer was cancelled or error
    }

    // Process the diagnostics after debounce
    asio::co_spawn(
        strand_,
        [this, uri]() -> asio::awaitable<void> {
          co_await ProcessDiagnosticsForUri(uri);
        },
        asio::detached);
  });
}

auto SlangdLspServer::ProcessDiagnosticsForUri(std::string uri)
    -> asio::awaitable<void> {
  auto file_opt = GetOpenFile(uri);
  if (!file_opt) {
    Logger()->debug("ProcessDiagnosticsForUri: File not open: {}", uri);
    co_return;
  }

  const auto& file = file_opt->get();
  Logger()->debug("Processing diagnostics for: {}", uri);

  // Use facade to compute diagnostics
  auto diagnostics =
      co_await language_service_->ComputeDiagnostics(uri, file.content);

  Logger()->debug("Publishing {} diagnostics for: {}", diagnostics.size(), uri);

  // Publish diagnostics as a single batch
  lsp::PublishDiagnosticsParams diag_params{
      .uri = uri, .version = file.version, .diagnostics = diagnostics};
  co_await PublishDiagnostics(diag_params);

  Logger()->debug("Completed publishing diagnostics for: {}", uri);

  // Remove the pending request if it exists
  pending_diagnostics_.erase(uri);
  co_return;
}

auto SlangdLspServer::IsConfigFile(const std::string& path) -> bool {
  return path.ends_with("/.slangd") || path.ends_with("\\.slangd");
}

}  // namespace slangd

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

// Threading notes:
// - GetOpenFile/AddOpenFile/RemoveOpenFile require strand synchronization
// - PublishDiagnostics does NOT (transport layer handles ordering)
// - language_service_->* methods handle their own synchronization
}  // namespace

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

    // Initialize language service with workspace via facade interface
    co_await language_service_->InitializeWorkspace(workspace_folder.uri);
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
        .id = std::string(kFileWatcherId),
        .method = std::string(kDidChangeWatchedFilesMethod),
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

  co_await language_service_->OnDocumentOpened(uri, text, version);
  Logger()->debug("SessionManager created session for: {}", uri);

  asio::co_spawn(
      executor_,
      [this, uri, text, version]() -> asio::awaitable<void> {
        co_await PublishDiagnosticsForDocument(uri, text, version);
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
    co_await asio::post(strand_, asio::use_awaitable);

    const auto& full_change =
        std::get<lsp::TextDocumentContentFullChangeEvent>(changes[0]);
    const auto& text = full_change.text;

    auto file_opt = GetOpenFile(uri);
    if (file_opt) {
      lsp::OpenFile& file = file_opt->get();
      file.content = text;
      file.version = version;
    }
  }
  // Diagnostics only computed on save to avoid expensive rebuilds

  co_return Ok();
}

auto SlangdLspServer::OnDidSaveTextDocument(
    lsp::DidSaveTextDocumentParams params)
    -> asio::awaitable<std::expected<void, lsp::LspError>> {
  Logger()->debug("SlangdLspServer OnDidSaveTextDocument");

  const auto& text_doc = params.textDocument;
  const auto& uri = text_doc.uri;

  co_await asio::post(strand_, asio::use_awaitable);

  auto file_opt = GetOpenFile(uri);
  if (file_opt) {
    const auto& file = file_opt->get();
    const std::string content = file.content;
    const int version = file.version;

    co_await asio::post(executor_, asio::use_awaitable);

    co_await language_service_->OnDocumentSaved(uri, content, version);
    Logger()->debug("SessionManager updated for: {}", uri);

    co_await ProcessDiagnosticsForUri(uri);
  }

  co_return Ok();
}

auto SlangdLspServer::OnDidCloseTextDocument(
    lsp::DidCloseTextDocumentParams params)
    -> asio::awaitable<std::expected<void, lsp::LspError>> {
  Logger()->debug("SlangdLspServer OnDidCloseTextDocument");

  const auto& uri = params.textDocument.uri;

  RemoveOpenFile(uri);

  language_service_->OnDocumentClosed(uri);
  Logger()->debug("SessionManager removed session for: {}", uri);

  co_return Ok();
}

auto SlangdLspServer::PublishDiagnosticsForDocument(
    std::string uri, std::string content, int version)
    -> asio::awaitable<void> {
  auto parse_result =
      co_await language_service_->ComputeParseDiagnostics(uri, content);
  if (parse_result.has_value()) {
    co_await PublishDiagnostics(
        {.uri = uri, .version = version, .diagnostics = *parse_result});
  }

  auto all_diags_result = co_await language_service_->ComputeDiagnostics(uri);

  if (!all_diags_result.has_value()) {
    Logger()->info(
        "PublishDiagnosticsForDocument: Session unavailable/cancelled, not "
        "publishing full diagnostics");
    co_return;
  }

  co_await PublishDiagnostics(
      {.uri = uri, .version = version, .diagnostics = *all_diags_result});
}

auto SlangdLspServer::ProcessDiagnosticsForUri(std::string uri)
    -> asio::awaitable<void> {
  co_await asio::post(strand_, asio::use_awaitable);
  auto file_opt = GetOpenFile(uri);
  if (!file_opt) {
    Logger()->debug("ProcessDiagnosticsForUri: File not open: {}", uri);
    co_return;
  }

  const auto& file = file_opt->get();
  int version = file.version;
  std::string content = file.content;

  co_await asio::post(executor_, asio::use_awaitable);

  co_await PublishDiagnosticsForDocument(uri, content, version);
}

auto SlangdLspServer::OnDocumentSymbols(lsp::DocumentSymbolParams params)
    -> asio::awaitable<
        std::expected<lsp::DocumentSymbolResult, lsp::LspError>> {
  Logger()->debug("SlangdLspServer OnDocumentSymbols");

  co_return co_await language_service_->GetDocumentSymbols(
      params.textDocument.uri);
}

auto SlangdLspServer::OnGotoDefinition(lsp::DefinitionParams params)
    -> asio::awaitable<std::expected<lsp::DefinitionResult, lsp::LspError>> {
  Logger()->debug("SlangdLspServer OnGotoDefinition");

  co_return co_await language_service_->GetDefinitionsForPosition(
      std::string(params.textDocument.uri), params.position);
}

auto SlangdLspServer::OnDidChangeWatchedFiles(
    lsp::DidChangeWatchedFilesParams params)
    -> asio::awaitable<std::expected<void, lsp::LspError>> {
  Logger()->debug("SlangdLspServer OnDidChangeWatchedFiles");

  asio::co_spawn(
      strand_,
      [this, params]() -> asio::awaitable<void> {
        bool has_config_change = false;
        bool has_sv_file_change = false;
        std::vector<std::string> changed_sv_uris;

        // Process each file change
        for (const auto& change : params.changes) {
          auto path = CanonicalPath::FromUri(change.uri);
          Logger()->debug("SlangdLspServer detected file change: {}", path);

          // Check if this is a config file change
          if (IsConfigFile(change.uri)) {
            has_config_change = true;
          }
          // Check if this is a SystemVerilog file change
          else if (IsSystemVerilogFile(path.Path())) {
            // Ignore open files - managed by LSP text sync
            if (GetOpenFile(change.uri)) {
              Logger()->debug(
                  "Ignoring watcher event for open file: {}", change.uri);
              continue;
            }
            has_sv_file_change = true;
            changed_sv_uris.push_back(change.uri);
          }
        }

        if (has_config_change) {
          language_service_->HandleConfigChange();
        }

        if (!changed_sv_uris.empty()) {
          language_service_->OnDocumentsChanged(changed_sv_uris);
          Logger()->debug(
              "SessionManager invalidated {} sessions", changed_sv_uris.size());
        }

        // Language service decides if rebuild needed
        if (has_sv_file_change) {
          for (const auto& change : params.changes) {
            auto path = CanonicalPath::FromUri(change.uri);
            if (IsSystemVerilogFile(path.Path())) {
              language_service_->HandleSourceFileChange(
                  change.uri, change.type);
            }
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

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

constexpr std::string_view kServerVersion = "0.1.0-alpha.2-dev";
constexpr std::string_view kFileWatcherId = "slangd-file-system-watcher";
constexpr std::string_view kDidChangeWatchedFilesMethod =
    "workspace/didChangeWatchedFiles";

// Status notification params
struct StatusParams {
  std::string status;

  [[maybe_unused]] friend void to_json(
      nlohmann::json& j, const StatusParams& p) {
    j = nlohmann::json{{"status", p.status}};
  }

  [[maybe_unused]] friend void from_json(
      const nlohmann::json& j, StatusParams& p) {
    j.at("status").get_to(p.status);
  }
};

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
  // Set up diagnostic publisher callback
  // LanguageService will use this to publish diagnostics extracted during
  // session creation (via hooks), eliminating race condition from cache
  // eviction
  language_service_->SetDiagnosticPublisher(
      [this](
          std::string uri, int version,
          std::vector<lsp::Diagnostic> diagnostics) {
        auto coroutine =
            [this, uri = std::move(uri), version,
             diagnostics = std::move(diagnostics)]() -> asio::awaitable<void> {
          co_await PublishDiagnostics(
              {.uri = uri, .version = version, .diagnostics = diagnostics});
        };
        asio::co_spawn(executor_, std::move(coroutine), asio::detached);
      });

  // Set up status publisher callback
  // LanguageService will use this to notify status changes (idle, indexing)
  language_service_->SetStatusPublisher([this](std::string status) {
    auto coroutine = [this,
                      status = std::move(status)]() -> asio::awaitable<void> {
      // Send custom notification to client
      StatusParams params{.status = status};
      co_await SendCustomNotification("$/slangd/status", params);
    };
    asio::co_spawn(executor_, std::move(coroutine), asio::detached);
  });
}

auto SlangdLspServer::OnInitialize(lsp::InitializeParams params)
    -> asio::awaitable<std::expected<lsp::InitializeResult, lsp::LspError>> {
  // Store workspace folder for initialization in OnInitialized
  // Return capabilities quickly - don't do heavy work here
  if (const auto& workspace_folders_opt = params.workspaceFolders) {
    if (workspace_folders_opt->size() != 1) {
      co_return LspError::UnexpectedFromCode(
          LspErrorCode::kInvalidRequest, "Only one workspace is supported");
    }

    workspace_folder_ = workspace_folders_opt->front();
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

  // Initialize workspace in background (syntax-based features work immediately)
  // Semantic features wait for workspace_ready internally
  if (workspace_folder_.has_value()) {
    asio::co_spawn(
        executor_,
        language_service_->InitializeWorkspace(workspace_folder_->uri),
        asio::detached);
  }

  // Register file system watcher dynamically
  auto register_watcher = [this]() -> asio::awaitable<void> {
    Logger()->info("Registering file system watcher");

    // Use RelativePattern to scope watchers to workspace folder
    lsp::FileSystemWatcher sv_files{
        .globPattern = lsp::RelativePattern{
            .baseUri = *workspace_folder_,
            .pattern = "**/*.{sv,svh,v,vh}",
        }};
    lsp::FileSystemWatcher slangd_config{
        .globPattern = lsp::RelativePattern{
            .baseUri = *workspace_folder_,
            .pattern = "**/.slangd",
        }};

    // Make registration ID unique per workspace to avoid conflicts
    std::string registration_id =
        std::string(kFileWatcherId) + "-" + workspace_folder_->uri;

    auto registration = lsp::Registration{
        .id = registration_id,
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
    } else {
      Logger()->info("File system watcher registered successfully");
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
  Logger()->info(
      "OnDidChangeWatchedFiles received: {} file change(s)",
      params.changes.size());
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
            // Process all file changes (watcher owns preamble rebuilds)
            co_await language_service_->HandleSourceFileChange(
                change.uri, change.type);
          }
        }

        // Config changes affect everything (search paths, macros, etc.)
        // HandleConfigChange rebuilds sessions with diagnostic hooks,
        // so diagnostics are published automatically
        if (has_config_change) {
          co_await language_service_->HandleConfigChange();
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

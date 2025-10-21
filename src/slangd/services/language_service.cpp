#include "slangd/services/language_service.hpp"

#include <slang/ast/symbols/CompilationUnitSymbols.h>
#include <slang/diagnostics/DiagnosticEngine.h>
#include <slang/syntax/SyntaxTree.h>

#include "slangd/semantic/diagnostic_converter.hpp"
#include "slangd/services/preamble_manager.hpp"
#include "slangd/utils/canonical_path.hpp"
#include "slangd/utils/scoped_timer.hpp"

namespace slangd::services {

using lsp::error::LspError;

LanguageService::LanguageService(
    asio::any_io_executor executor, std::shared_ptr<spdlog::logger> logger)
    : preamble_manager_(nullptr),
      logger_(logger ? logger : spdlog::default_logger()),
      executor_(executor),
      open_tracker_(std::make_shared<OpenDocumentTracker>()),
      doc_state_(executor, open_tracker_),
      compilation_pool_(std::make_unique<asio::thread_pool>(kThreadPoolSize)) {
  logger_->debug(
      "LanguageService created with {} compilation threads", kThreadPoolSize);
}

auto LanguageService::CreateDiagnosticHook(std::string uri, int version)
    -> std::function<void(const CompilationState&)> {
  return [this, uri = std::move(uri), version](const CompilationState& state) {
    // Extract diagnostics (on strand, session cannot be cleaned up)
    auto parse_diagnostics =
        semantic::DiagnosticConverter::ExtractParseDiagnostics(
            *state.compilation, *state.compilation->getSourceManager(),
            state.main_buffer_id, logger_);

    auto semantic_diagnostics =
        semantic::DiagnosticConverter::ExtractCollectedDiagnostics(
            *state.compilation, *state.compilation->getSourceManager(),
            state.main_buffer_id);

    // Combine diagnostics
    parse_diagnostics.insert(
        parse_diagnostics.end(), semantic_diagnostics.begin(),
        semantic_diagnostics.end());

    // Post back to main thread to publish
    if (diagnostic_publisher_) {
      asio::post(
          executor_,
          [this, uri, version, diagnostics = std::move(parse_diagnostics)]() {
            diagnostic_publisher_(uri, version, diagnostics);
          });
    }
  };
}

auto LanguageService::InitializeWorkspace(std::string workspace_uri)
    -> asio::awaitable<void> {
  utils::ScopedTimer timer("Workspace initialization", logger_);
  logger_->debug("LanguageService initializing workspace: {}", workspace_uri);

  auto workspace_path = CanonicalPath::FromUri(workspace_uri);
  layout_service_ =
      ProjectLayoutService::Create(executor_, workspace_path, logger_);
  co_await layout_service_->LoadConfig(workspace_path);

  preamble_manager_ = co_await PreambleManager::CreateFromProjectLayout(
      layout_service_, compilation_pool_->get_executor(), logger_);

  if (preamble_manager_) {
    logger_->debug(
        "LanguageService created PreambleManager with {} packages",
        preamble_manager_->GetPackageMap().size());
  } else {
    logger_->error("LanguageService failed to create PreambleManager");
  }

  session_manager_ = std::make_unique<SessionManager>(
      executor_, layout_service_, preamble_manager_, open_tracker_, logger_);

  auto elapsed = timer.GetElapsed();
  logger_->info(
      "LanguageService workspace initialized: {} ({})", workspace_uri,
      utils::ScopedTimer::FormatDuration(elapsed));
}

auto LanguageService::ComputeParseDiagnostics(
    std::string uri, std::string content)
    -> asio::awaitable<std::expected<std::vector<lsp::Diagnostic>, LspError>> {
  utils::ScopedTimer timer("ComputeParseDiagnostics", logger_);

  if (!layout_service_) {
    logger_->error("LanguageService: Workspace not initialized");
    co_return LspError::UnexpectedFromCode(
        lsp::error::LspErrorCode::kInternalError, "Workspace not initialized");
  }

  logger_->debug(
      "LanguageService computing parse diagnostics (single-file mode): {}",
      uri);

  // Build single-file compilation and extract diagnostics in thread pool
  auto diagnostics = co_await asio::co_spawn(
      compilation_pool_->get_executor(),
      [this, uri, content]() -> asio::awaitable<std::vector<lsp::Diagnostic>> {
        // Build parse-only compilation (no preamble_manager → single file only)
        auto [source_manager, compilation, main_buffer_id] =
            OverlaySession::BuildCompilation(
                uri, content, layout_service_,
                nullptr,  // Single-file mode
                logger_);

        // Extract parse diagnostics using BufferID for fast filtering
        co_return semantic::DiagnosticConverter::ExtractParseDiagnostics(
            *compilation, *source_manager, main_buffer_id, logger_);
      },
      asio::use_awaitable);

  // Post result back to main strand
  co_await asio::post(executor_, asio::use_awaitable);

  logger_->debug(
      "LanguageService computed {} parse diagnostics for: {}",
      diagnostics.size(), uri);

  co_return diagnostics;
}

auto LanguageService::GetDefinitionsForPosition(
    std::string uri, lsp::Position position)
    -> asio::awaitable<std::expected<std::vector<lsp::Location>, LspError>> {
  utils::ScopedTimer timer("GetDefinitionsForPosition", logger_);

  if (!layout_service_) {
    logger_->error("LanguageService: Workspace not initialized");
    co_return LspError::UnexpectedFromCode(
        lsp::error::LspErrorCode::kInternalError, "Workspace not initialized");
  }

  auto result = co_await session_manager_->WithSession(
      uri, [this, uri, position](const OverlaySession& session) {
        auto def_loc_opt =
            session.GetSemanticIndex().LookupDefinitionAt(uri, position);

        if (!def_loc_opt) {
          logger_->debug(
              "No definition found at position {}:{} in {}", position.line,
              position.character, uri);
          return std::vector<lsp::Location>{};
        }

        return std::vector<lsp::Location>{*def_loc_opt};
      });

  if (!result) {
    // Return empty instead of error - prevents VSCode spinning
    logger_->debug(
        "GetDefinitionsForPosition failed for {}: {}", uri, result.error());
    co_return std::vector<lsp::Location>{};
  }

  co_return *result;
}

auto LanguageService::GetDocumentSymbols(std::string uri) -> asio::awaitable<
    std::expected<std::vector<lsp::DocumentSymbol>, lsp::error::LspError>> {
  utils::ScopedTimer timer("GetDocumentSymbols", logger_);

  if (!layout_service_) {
    logger_->error("LanguageService: Workspace not initialized");
    co_return LspError::UnexpectedFromCode(
        lsp::error::LspErrorCode::kInternalError, "Workspace not initialized");
  }

  auto result = co_await session_manager_->WithSession(
      uri, [uri](const OverlaySession& session) {
        return session.GetSemanticIndex().GetDocumentSymbols(uri);
      });

  if (!result) {
    // Return empty instead of error - prevents VSCode spinning
    logger_->debug("GetDocumentSymbols failed for {}: {}", uri, result.error());
    co_return std::vector<lsp::DocumentSymbol>{};
  }

  co_return *result;
}

auto LanguageService::HandleConfigChange() -> asio::awaitable<void> {
  if (!layout_service_) {
    co_return;
  }

  layout_service_->RebuildLayout();

  // Rebuild PreambleManager with new configuration (search paths, macros, etc.)
  preamble_manager_ = co_await PreambleManager::CreateFromProjectLayout(
      layout_service_, compilation_pool_->get_executor(), logger_);

  if (preamble_manager_) {
    logger_->debug(
        "LanguageService rebuilt PreambleManager with {} packages",
        preamble_manager_->GetPackageMap().size());
  } else {
    logger_->error("LanguageService failed to rebuild PreambleManager");
  }

  // Update SessionManager's preamble_manager reference for future session
  // creations
  session_manager_->UpdatePreambleManager(preamble_manager_);

  session_manager_->InvalidateAllSessions();

  // Rebuild sessions for all open files to restore LSP features immediately
  auto open_uris = co_await doc_state_.GetAllUris();
  logger_->debug(
      "LanguageService rebuilding {} open file sessions after config change",
      open_uris.size());

  for (const auto& uri : open_uris) {
    auto state = co_await doc_state_.Get(uri);
    if (state) {
      co_await session_manager_->UpdateSession(
          uri, state->content, state->version,
          CreateDiagnosticHook(uri, state->version));
    }
  }

  logger_->info("LanguageService completed config change rebuild");
}

auto LanguageService::HandleSourceFileChange(
    std::string uri, lsp::FileChangeType change_type) -> asio::awaitable<void> {
  if (!layout_service_) {
    co_return;
  }

  switch (change_type) {
    case lsp::FileChangeType::kCreated:
    case lsp::FileChangeType::kDeleted:
      // Structural changes require layout rebuild
      layout_service_->ScheduleDebouncedRebuild();
      session_manager_->InvalidateAllSessions();
      logger_->debug(
          "LanguageService handled structural change: {} ({})", uri,
          static_cast<int>(change_type));
      break;

    case lsp::FileChangeType::kChanged:
      // Conservative invalidation: Any file might be a package/interface
      // included in all OverlaySessions. Without dependency tracking,
      // invalidate all sessions to ensure correctness.
      session_manager_->InvalidateAllSessions();
      logger_->debug(
          "LanguageService invalidated all sessions due to file change: {}",
          uri);
      break;
  }
}

// Document lifecycle events (protocol-level API)
auto LanguageService::OnDocumentOpened(
    std::string uri, std::string content, int version)
    -> asio::awaitable<void> {
  if (!session_manager_) {
    logger_->error("LanguageService: SessionManager not initialized");
    co_return;
  }

  // Create session with diagnostic hook
  co_await session_manager_->UpdateSession(
      uri, content, version, CreateDiagnosticHook(uri, version));

  // Store document state after (less time-critical)
  co_await doc_state_.Update(uri, content, version);
}

auto LanguageService::OnDocumentChanged(
    std::string uri, std::string content, int version)
    -> asio::awaitable<void> {
  if (!session_manager_) {
    logger_->error("LanguageService: SessionManager not initialized");
    co_return;
  }

  // Store document state only (no session rebuild - typing is fast!)
  co_await doc_state_.Update(uri, content, version);
}

auto LanguageService::OnDocumentSaved(std::string uri)
    -> asio::awaitable<void> {
  if (!session_manager_) {
    logger_->error("LanguageService: SessionManager not initialized");
    co_return;
  }

  // Get document state
  auto doc_state = co_await doc_state_.Get(uri);
  if (!doc_state) {
    logger_->error("LanguageService: No document state for {}", uri);
    co_return;
  }

  // Rebuild session with saved content and diagnostic hook
  co_await session_manager_->UpdateSession(
      uri, doc_state->content, doc_state->version,
      CreateDiagnosticHook(uri, doc_state->version));
}

auto LanguageService::OnDocumentClosed(std::string uri) -> void {
  if (!session_manager_) {
    logger_->error("LanguageService: SessionManager not initialized");
    return;
  }

  // Cancel pending compilation to prevent unbounded memory accumulation
  // (preview mode spam defense - see docs/SESSION_MANAGEMENT.md)
  session_manager_->CancelPendingSession(uri);

  // Remove document state asynchronously
  asio::co_spawn(
      executor_,
      [this, uri]() -> asio::awaitable<void> {
        co_await doc_state_.Remove(uri);
      },
      asio::detached);

  // Schedule cleanup of session after delay
  // Supports prefetch pattern: if reopened within 5s, reuse stored session
  session_manager_->ScheduleCleanup(uri);
}

auto LanguageService::OnDocumentsChanged(std::vector<std::string> /*uris*/)
    -> void {
  // Reserved for future use - currently no external callers
  // Sessions are managed through OnDocumentOpened/Changed/Saved/Closed
  // lifecycle
}

auto LanguageService::IsDocumentOpen(const std::string& uri) const -> bool {
  return open_tracker_->Contains(uri);
}

}  // namespace slangd::services

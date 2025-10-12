#include "slangd/services/language_service.hpp"

#include <slang/ast/symbols/CompilationUnitSymbols.h>
#include <slang/diagnostics/DiagnosticEngine.h>
#include <slang/syntax/SyntaxTree.h>

#include "slangd/semantic/diagnostic_converter.hpp"
#include "slangd/services/global_catalog.hpp"
#include "slangd/utils/canonical_path.hpp"
#include "slangd/utils/conversion.hpp"
#include "slangd/utils/scoped_timer.hpp"

namespace slangd::services {

using lsp::error::LspError;

LanguageService::LanguageService(
    asio::any_io_executor executor, std::shared_ptr<spdlog::logger> logger)
    : global_catalog_(nullptr),
      logger_(logger ? logger : spdlog::default_logger()),
      executor_(executor),
      open_tracker_(std::make_shared<OpenDocumentTracker>()),
      doc_state_(executor, open_tracker_),
      compilation_pool_(std::make_unique<asio::thread_pool>(kThreadPoolSize)) {
  logger_->debug(
      "LanguageService created with {} compilation threads", kThreadPoolSize);
}

auto LanguageService::InitializeWorkspace(std::string workspace_uri)
    -> asio::awaitable<void> {
  utils::ScopedTimer timer("Workspace initialization", logger_);
  logger_->debug("LanguageService initializing workspace: {}", workspace_uri);

  auto workspace_path = CanonicalPath::FromUri(workspace_uri);
  layout_service_ =
      ProjectLayoutService::Create(executor_, workspace_path, logger_);
  co_await layout_service_->LoadConfig(workspace_path);

  global_catalog_ =
      GlobalCatalog::CreateFromProjectLayout(layout_service_, logger_);

  if (global_catalog_) {
    logger_->debug(
        "LanguageService created GlobalCatalog with {} packages, version {}",
        global_catalog_->GetPackages().size(), global_catalog_->GetVersion());
  } else {
    logger_->error("LanguageService failed to create GlobalCatalog");
  }

  session_manager_ = std::make_unique<SessionManager>(
      executor_, layout_service_, global_catalog_, open_tracker_, logger_);

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
        // Build parse-only compilation (no catalog → single file only)
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

auto LanguageService::ComputeDiagnostics(std::string uri)
    -> asio::awaitable<std::expected<std::vector<lsp::Diagnostic>, LspError>> {
  utils::ScopedTimer timer("ComputeDiagnostics", logger_);

  if (!layout_service_) {
    logger_->error("LanguageService: Workspace not initialized");
    co_return LspError::UnexpectedFromCode(
        lsp::error::LspErrorCode::kInternalError, "Workspace not initialized");
  }

  // Use callback pattern to access compilation state without shared_ptr escape
  auto result = co_await session_manager_->WithCompilationState(
      uri, [this, uri](const CompilationState& state) {
        // Extract parse diagnostics (syntax errors from syntax trees)
        auto parse_diags =
            semantic::DiagnosticConverter::ExtractParseDiagnostics(
                *state.compilation, *state.source_manager, state.main_buffer_id,
                logger_);

        // Extract semantic diagnostics (from diagMap, populated by
        // forceElaborate) Pass GlobalCatalog to filter false-positive
        // UnknownModule diagnostics
        auto semantic_diags =
            semantic::DiagnosticConverter::ExtractCollectedDiagnostics(
                *state.compilation, *state.source_manager, state.main_buffer_id,
                global_catalog_.get());

        // Combine parse + semantic diagnostics
        parse_diags.insert(
            parse_diags.end(), semantic_diags.begin(), semantic_diags.end());

        return parse_diags;
      });

  if (!result) {
    logger_->debug(
        "LanguageService: No compilation state for {} ({})", uri,
        result.error());
    co_return LspError::UnexpectedFromCode(
        lsp::error::LspErrorCode::kInternalError, result.error());
  }

  logger_->debug(
      "LanguageService computed {} diagnostics for: {} ({})", result->size(),
      uri, utils::ScopedTimer::FormatDuration(timer.GetElapsed()));

  co_return *result;
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

  // Use callback pattern to access session without shared_ptr escape
  auto result = co_await session_manager_->WithSession(
      uri, [this, uri, position](const OverlaySession& session) {
        // Get source manager and convert position to location
        const auto& source_manager = session.GetSourceManager();
        auto buffers = source_manager.getAllBuffers();
        if (buffers.empty()) {
          logger_->warn("No buffers found in source manager for: {}", uri);
          return std::vector<lsp::Location>{};
        }

        // Find the buffer that matches the requested URI
        auto target_path = CanonicalPath::FromUri(uri);
        slang::BufferID target_buffer;
        bool found_buffer = false;

        for (const auto& buffer_id : buffers) {
          auto buffer_path = source_manager.getFullPath(buffer_id);
          if (buffer_path == target_path.String()) {
            target_buffer = buffer_id;
            found_buffer = true;
            break;
          }
        }

        if (!found_buffer) {
          logger_->warn(
              "No buffer found matching URI: {} (path: {}), using fallback",
              uri, target_path.String());
          // Fallback to first buffer (old behavior)
          target_buffer = buffers[0];
        }

        auto location = ConvertLspPositionToSlangLocation(
            position, target_buffer, source_manager);

        // Look up definition using semantic index
        auto def_loc_opt =
            session.GetSemanticIndex().LookupDefinitionAt(location);
        if (!def_loc_opt) {
          logger_->debug(
              "No definition found at position {}:{} in {}", position.line,
              position.character, uri);
          return std::vector<lsp::Location>{};
        }

        // Convert to LSP location based on definition type
        lsp::Location lsp_location;
        if (def_loc_opt->cross_file_path.has_value()) {
          // Cross-file definition - use pre-converted path and range
          lsp_location.uri = def_loc_opt->cross_file_path->ToUri();
          lsp_location.range = *def_loc_opt->cross_file_range;
        } else {
          // Same-file definition - convert using current source manager
          lsp_location = ConvertSlangLocationToLspLocation(
              def_loc_opt->same_file_range->start(), source_manager);
          lsp_location.range = ConvertSlangRangeToLspRange(
              *def_loc_opt->same_file_range, source_manager);
        }

        return std::vector<lsp::Location>{lsp_location};
      });

  if (!result) {
    logger_->debug(
        "GetDefinitionsForPosition: Session not found for {} ({}), returning "
        "error",
        uri, result.error());
    co_return LspError::UnexpectedFromCode(
        lsp::error::LspErrorCode::kInternalError, result.error());
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

  // Use callback pattern to access session without shared_ptr escape
  auto result = co_await session_manager_->WithSession(
      uri, [uri](const OverlaySession& session) {
        return session.GetSemanticIndex().GetDocumentSymbols(uri);
      });

  if (!result) {
    logger_->debug(
        "GetDocumentSymbols: Session not found for {} ({}), returning error",
        uri, result.error());
    co_return LspError::UnexpectedFromCode(
        lsp::error::LspErrorCode::kInternalError, result.error());
  }

  co_return *result;
}

auto LanguageService::HandleConfigChange() -> asio::awaitable<void> {
  if (!layout_service_) {
    co_return;
  }

  layout_service_->RebuildLayout();

  // Rebuild GlobalCatalog with new configuration (search paths, macros, etc.)
  global_catalog_ =
      GlobalCatalog::CreateFromProjectLayout(layout_service_, logger_);

  if (global_catalog_) {
    logger_->debug(
        "LanguageService rebuilt GlobalCatalog with {} packages, version {}",
        global_catalog_->GetPackages().size(), global_catalog_->GetVersion());
  } else {
    logger_->error("LanguageService failed to rebuild GlobalCatalog");
  }

  // Update SessionManager's catalog reference for future session creations
  session_manager_->UpdateCatalog(global_catalog_);

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
          uri, state->content, state->version);
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

  // Create session first (time-critical - other handlers may need it)
  co_await session_manager_->UpdateSession(uri, content, version);

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

  // Rebuild session with saved content
  co_await session_manager_->UpdateSession(
      uri, doc_state->content, doc_state->version);
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

  // Keep completed sessions in cache for close/reopen optimization
  // LRU eviction will handle cleanup when cache size limit is reached
}

auto LanguageService::OnDocumentsChanged(std::vector<std::string> uris)
    -> void {
  if (!session_manager_) {
    logger_->error("LanguageService: SessionManager not initialized");
    return;
  }

  logger_->debug("LanguageService::OnDocumentsChanged: {} files", uris.size());
  session_manager_->InvalidateSessions(uris);
  logger_->debug("SessionManager cache invalidated for {} files", uris.size());
}

auto LanguageService::GetDocumentState(std::string uri)
    -> asio::awaitable<std::optional<DocumentState>> {
  co_return co_await doc_state_.Get(uri);
}

auto LanguageService::GetAllOpenDocumentUris()
    -> asio::awaitable<std::vector<std::string>> {
  co_return co_await doc_state_.GetAllUris();
}

}  // namespace slangd::services

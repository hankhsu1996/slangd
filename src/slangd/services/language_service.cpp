#include "slangd/services/language_service.hpp"

#include <slang/ast/symbols/CompilationUnitSymbols.h>
#include <slang/diagnostics/DiagnosticEngine.h>
#include <slang/syntax/SyntaxTree.h>

#include "slangd/semantic/diagnostic_converter.hpp"
#include "slangd/services/preamble_manager.hpp"
#include "slangd/syntax/syntax_document_symbol_visitor.hpp"
#include "slangd/utils/canonical_path.hpp"
#include "slangd/utils/compilation_options.hpp"
#include "slangd/utils/path_utils.hpp"
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
      config_ready_(executor),
      workspace_ready_(executor),
      compilation_pool_(
          std::make_unique<asio::thread_pool>(GetThreadPoolSize())) {
  logger_->debug(
      "LanguageService created with {} compilation threads",
      GetThreadPoolSize());
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

  // Notify status: indexing started (before any work)
  if (status_publisher_) {
    status_publisher_("indexing");
  }

  workspace_root_ = CanonicalPath::FromUri(workspace_uri);
  layout_service_ =
      ProjectLayoutService::Create(executor_, workspace_root_, logger_);
  co_await layout_service_->LoadConfig(workspace_root_);

  // Config loaded: syntax features can now use defines for ifdef/ifndef
  // handling
  config_ready_.Set();
  logger_->debug("LanguageService config loaded (syntax features ready)");

  auto preamble_result = co_await PreambleManager::CreateFromProjectLayout(
      layout_service_, compilation_pool_->get_executor(), logger_);

  if (!preamble_result) {
    logger_->warn(
        "LanguageService preamble creation failed: {} (continuing without "
        "preamble)",
        preamble_result.error());
    preamble_manager_ = nullptr;
  } else {
    preamble_manager_ = *preamble_result;
    logger_->debug(
        "LanguageService created PreambleManager ({} packages, {} definitions)",
        preamble_manager_->GetPackageMap().size(),
        preamble_manager_->GetDefinitionMap().size());
  }

  session_manager_ = std::make_unique<SessionManager>(
      executor_, layout_service_, preamble_manager_, open_tracker_, logger_);

  // Notify status: indexing completed
  if (status_publisher_) {
    status_publisher_("idle");
  }

  // Signal workspace ready - wakes all waiting handlers
  workspace_ready_.Set();

  auto elapsed = timer.GetElapsed();
  logger_->info(
      "LanguageService workspace initialized: {} ({})", workspace_uri,
      utils::ScopedTimer::FormatDuration(elapsed));
}

auto LanguageService::ComputeParseDiagnostics(
    std::string uri, std::string content)
    -> asio::awaitable<std::expected<std::vector<lsp::Diagnostic>, LspError>> {
  utils::ScopedTimer timer("ComputeParseDiagnostics", logger_);

  // Wait for workspace initialization to complete
  co_await workspace_ready_.AsyncWait(asio::use_awaitable);

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

        // Keep as unique_ptr - temporary use, destroyed after extraction
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

  // Wait for workspace initialization to complete
  co_await workspace_ready_.AsyncWait(asio::use_awaitable);

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
  utils::ScopedTimer timer("GetDocumentSymbols (syntax)", logger_);

  // Get file content from open documents
  auto doc_state = co_await doc_state_.Get(uri);
  if (!doc_state) {
    logger_->debug("GetDocumentSymbols: document not open: {}", uri);
    co_return std::vector<lsp::DocumentSymbol>{};
  }

  // Wait for config to be loaded (needed for correct ifdef/ifndef handling)
  co_await config_ready_.AsyncWait(asio::use_awaitable);

  // Parse syntax tree directly (no session/preamble needed)
  auto source_manager = std::make_shared<slang::SourceManager>();
  auto options = utils::CreateLspCompilationOptions();

  // Add project-specific preprocessor defines (for ifdef/ifndef support)
  auto pp_options = options.getOrDefault<slang::parsing::PreprocessorOptions>();
  for (const auto& define : layout_service_->GetDefines()) {
    pp_options.predefines.push_back(define);
  }
  options.set(pp_options);

  auto buffer = source_manager->assignText(uri, doc_state->content);
  auto syntax_tree =
      slang::syntax::SyntaxTree::fromBuffer(buffer, *source_manager, options);

  if (!syntax_tree) {
    logger_->error("GetDocumentSymbols: failed to parse syntax tree: {}", uri);
    co_return std::vector<lsp::DocumentSymbol>{};
  }

  // Direct syntax traversal
  syntax::SyntaxSymbolVisitor visitor(uri, *source_manager, buffer.id);
  syntax_tree->root().visit(visitor);

  co_return visitor.GetResult();
}

auto LanguageService::RebuildPreambleAndSessions() -> asio::awaitable<void> {
  // Protection: Don't start if already rebuilding
  if (preamble_rebuild_in_progress_) {
    preamble_rebuild_pending_ = true;
    logger_->debug("Preamble rebuild in progress, marked as pending");
    co_return;
  }

  preamble_rebuild_in_progress_ = true;
  preamble_rebuild_pending_ = false;  // Clear before rebuild

  // Notify status: indexing started
  if (status_publisher_) {
    status_publisher_("indexing");
  }

  // Layout is maintained incrementally by HandleSourceFileChange
  // Config changes rebuild layout via HandleConfigFileChange

  // Rebuild PreambleManager with current configuration
  auto preamble_result = co_await PreambleManager::CreateFromProjectLayout(
      layout_service_, compilation_pool_->get_executor(), logger_);

  if (!preamble_result) {
    logger_->warn(
        "LanguageService preamble rebuild failed: {} (continuing without "
        "preamble)",
        preamble_result.error());
    preamble_manager_ = nullptr;
  } else {
    // Move from result to avoid holding extra shared_ptr copy
    preamble_manager_ = std::move(*preamble_result);
    logger_->debug(
        "LanguageService rebuilt PreambleManager ({} packages, {} "
        "definitions)",
        preamble_manager_->GetPackageMap().size(),
        preamble_manager_->GetDefinitionMap().size());

    // Atomic swap - await to ensure update completes before invalidation
    co_await session_manager_->UpdatePreambleManager(preamble_manager_);
    co_await session_manager_->InvalidateAllSessions();
  }

  // Rebuild sessions to republish diagnostics (server-push)
  // Client doesn't know preamble was rebuilt, so we must push updates
  auto open_uris = co_await doc_state_.GetAllUris();

  for (const auto& uri : open_uris) {
    auto state = co_await doc_state_.Get(uri);
    if (state) {
      co_await session_manager_->UpdateSession(
          uri, state->content, state->version,
          CreateDiagnosticHook(uri, state->version));
    }
  }

  logger_->info("LanguageService completed preamble rebuild");

  // Notify status: indexing completed
  if (status_publisher_) {
    status_publisher_("idle");
  }

  // If more saves happened during rebuild, schedule next rebuild
  // Use debounce delay to allow session tasks to complete (avoid overlap)
  if (preamble_rebuild_pending_) {
    logger_->debug(
        "More saves happened during rebuild, scheduling next rebuild");
    preamble_rebuild_pending_ = false;
    ScheduleDebouncedPreambleRebuild();
  }

  preamble_rebuild_in_progress_ = false;
}

auto LanguageService::HandleConfigChange() -> asio::awaitable<void> {
  co_await workspace_ready_.AsyncWait(asio::use_awaitable);

  // Reload config and rebuild layout (includes full directory scan)
  auto config_path = workspace_root_ / ".slangd";
  co_await layout_service_->HandleConfigFileChange(config_path);

  // Rebuild preamble with new config (layout already rebuilt)
  co_await RebuildPreambleAndSessions();
}

auto LanguageService::ScheduleDebouncedPreambleRebuild() -> void {
  logger_->debug(
      "LanguageService: Scheduling debounced preamble rebuild ({}ms delay)",
      kPreambleDebounceDelay.count());

  // Cancel existing timer if any
  if (preamble_rebuild_timer_) {
    preamble_rebuild_timer_->cancel();
  }

  // Create new timer
  preamble_rebuild_timer_ =
      asio::steady_timer(executor_, kPreambleDebounceDelay);
  preamble_rebuild_timer_->async_wait([this](std::error_code ec) {
    if (!ec) {
      logger_->debug(
          "LanguageService: Preamble debounce timer expired, triggering "
          "rebuild");
      asio::co_spawn(
          executor_,
          [this]() -> asio::awaitable<void> {
            co_await RebuildPreambleAndSessions();
          },
          asio::detached);
    }
  });
}

auto LanguageService::HandleSourceFileChange(
    std::string uri, lsp::FileChangeType change_type) -> asio::awaitable<void> {
  co_await workspace_ready_.AsyncWait(asio::use_awaitable);

  auto path = CanonicalPath::FromUri(uri);
  if (!IsSystemVerilogFile(path.Path())) {
    co_return;
  }

  // Update layout incrementally based on change type
  switch (change_type) {
    case lsp::FileChangeType::kCreated:
      // File created - add to layout if AutoDiscover enabled
      co_await layout_service_->AddFile(path);
      break;

    case lsp::FileChangeType::kDeleted:
      // File deleted - remove from layout
      co_await layout_service_->RemoveFile(path);
      break;

    case lsp::FileChangeType::kChanged:
      // File modified - no layout update needed
      break;
  }

  // All changes may affect preamble (packages, interfaces)
  ScheduleDebouncedPreambleRebuild();
}

// Document lifecycle events (protocol-level API)
auto LanguageService::OnDocumentOpened(
    std::string uri, std::string content, int version)
    -> asio::awaitable<void> {
  // Store document state first
  co_await doc_state_.Update(uri, content, version);

  // Wait for workspace initialization to complete
  co_await workspace_ready_.AsyncWait(asio::use_awaitable);

  // Create session with diagnostic hook
  co_await session_manager_->UpdateSession(
      uri, content, version, CreateDiagnosticHook(uri, version));
}

auto LanguageService::OnDocumentChanged(
    std::string uri, std::string content, int version)
    -> asio::awaitable<void> {
  // Wait for workspace initialization to complete
  co_await workspace_ready_.AsyncWait(asio::use_awaitable);

  // Store document state
  co_await doc_state_.Update(uri, content, version);

  // Schedule debounced session rebuild with diagnostics
  ScheduleDebouncedSessionRebuild(uri);
}

auto LanguageService::RebuildSessionWithDiagnostics(std::string uri)
    -> asio::awaitable<void> {
  // Protection: Don't start if already rebuilding
  if (session_rebuild_state_[uri] != RebuildState::kIdle) {
    session_rebuild_state_[uri] = RebuildState::kPendingNext;
    logger_->debug(
        "Session rebuild in progress for {}, marked as pending", uri);
    co_return;
  }

  session_rebuild_state_[uri] = RebuildState::kInProgress;

  // Get current document state
  auto doc_state = co_await doc_state_.Get(uri);
  if (!doc_state) {
    logger_->debug("RebuildSessionWithDiagnostics: document not open: {}", uri);
    session_rebuild_state_[uri] = RebuildState::kIdle;
    co_return;
  }

  // Rebuild session with diagnostic hook
  co_await session_manager_->UpdateSession(
      uri, doc_state->content, doc_state->version,
      CreateDiagnosticHook(uri, doc_state->version));

  // Check if more changes happened during rebuild
  if (session_rebuild_state_[uri] == RebuildState::kPendingNext) {
    session_rebuild_state_[uri] = RebuildState::kIdle;
    logger_->debug(
        "More changes happened during rebuild for {}, rebuilding immediately",
        uri);
    // Rebuild immediately - debounce already happened when timer fired
    asio::co_spawn(
        executor_,
        [this, uri]() -> asio::awaitable<void> {
          co_await RebuildSessionWithDiagnostics(uri);
        },
        asio::detached);
  } else {
    session_rebuild_state_[uri] = RebuildState::kIdle;
  }
}

auto LanguageService::ScheduleDebouncedSessionRebuild(std::string uri) -> void {
  logger_->debug(
      "Scheduling debounced session rebuild for {} ({}ms delay)", uri,
      kSessionDebounceDelay.count());

  // Cancel existing timer if any
  auto it = session_rebuild_timers_.find(uri);
  if (it != session_rebuild_timers_.end()) {
    it->second.cancel();
    session_rebuild_timers_.erase(it);
  }

  // Create new timer with executor
  auto [timer_it, inserted] =
      session_rebuild_timers_.try_emplace(uri, executor_);
  timer_it->second.expires_after(kSessionDebounceDelay);
  timer_it->second.async_wait([this, uri](std::error_code ec) {
    if (!ec) {
      logger_->debug(
          "Session debounce timer expired for {}, triggering rebuild", uri);
      asio::co_spawn(
          executor_,
          [this, uri]() -> asio::awaitable<void> {
            co_await RebuildSessionWithDiagnostics(uri);
          },
          asio::detached);
    }
  });
}

auto LanguageService::OnDocumentSaved(std::string uri)
    -> asio::awaitable<void> {
  // Wait for workspace initialization to complete
  co_await workspace_ready_.AsyncWait(asio::use_awaitable);

  // Cancel any pending debounced rebuild for this file (save takes priority)
  auto timer_it = session_rebuild_timers_.find(uri);
  if (timer_it != session_rebuild_timers_.end()) {
    timer_it->second.cancel();
  }

  // Get document state
  auto doc_state = co_await doc_state_.Get(uri);
  if (!doc_state) {
    logger_->error("LanguageService: No document state for {}", uri);
    co_return;
  }

  // Rebuild overlay session only (preamble handled by file watcher)
  co_await session_manager_->UpdateSession(
      uri, doc_state->content, doc_state->version,
      CreateDiagnosticHook(uri, doc_state->version));
}

auto LanguageService::OnDocumentClosed(std::string uri) -> void {
  // If workspace not ready yet, nothing to clean up
  if (!workspace_ready_.IsSet()) {
    return;
  }

  // Cancel pending debounced rebuild
  auto timer_it = session_rebuild_timers_.find(uri);
  if (timer_it != session_rebuild_timers_.end()) {
    timer_it->second.cancel();
    session_rebuild_timers_.erase(timer_it);
  }

  // Clean up rebuild state
  session_rebuild_state_.erase(uri);

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

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

LanguageService::LanguageService(
    asio::any_io_executor executor, std::shared_ptr<spdlog::logger> logger)
    : global_catalog_(nullptr),
      logger_(logger ? logger : spdlog::default_logger()),
      executor_(std::move(executor)),
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
      executor_, layout_service_, global_catalog_, logger_);

  auto elapsed = timer.GetElapsed();
  logger_->info(
      "LanguageService workspace initialized: {} ({})", workspace_uri,
      utils::ScopedTimer::FormatDuration(elapsed));
}

auto LanguageService::ComputeParseDiagnostics(
    std::string uri, std::string content)
    -> asio::awaitable<std::vector<lsp::Diagnostic>> {
  utils::ScopedTimer timer("ComputeParseDiagnostics", logger_);

  if (!layout_service_) {
    logger_->error("LanguageService: Workspace not initialized");
    co_return std::vector<lsp::Diagnostic>{};
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
    -> asio::awaitable<std::vector<lsp::Diagnostic>> {
  utils::ScopedTimer timer("ComputeDiagnostics", logger_);

  if (!layout_service_) {
    logger_->error("LanguageService: Workspace not initialized");
    co_return std::vector<lsp::Diagnostic>{};
  }

  logger_->debug("LanguageService computing diagnostics for: {}", uri);

  // Get full session from SessionManager (includes diagnostics from indexing)
  auto session = co_await session_manager_->GetSession(uri);
  if (!session) {
    logger_->error("No session available for: {}", uri);
    co_return std::vector<lsp::Diagnostic>{};
  }

  // Extract diagnostics from session (already populated during indexing)
  auto diagnostics = semantic::DiagnosticConverter::ExtractDiagnostics(
      session->GetDiagnostics(), session->GetSourceManager(),
      session->GetMainBufferID());
  auto filtered =
      semantic::DiagnosticConverter::FilterDiagnostics(std::move(diagnostics));

  logger_->debug(
      "LanguageService computed {} diagnostics for: {} ({})", filtered.size(),
      uri, utils::ScopedTimer::FormatDuration(timer.GetElapsed()));

  co_return filtered;
}

auto LanguageService::GetDefinitionsForPosition(
    std::string uri, lsp::Position position)
    -> asio::awaitable<std::vector<lsp::Location>> {
  utils::ScopedTimer timer("GetDefinitionsForPosition", logger_);

  if (!layout_service_) {
    logger_->error("LanguageService: Workspace not initialized");
    co_return std::vector<lsp::Location>{};
  }

  auto session = co_await session_manager_->GetSession(uri);
  if (!session) {
    logger_->debug("No session for {}, returning empty definitions", uri);
    co_return std::vector<lsp::Location>{};
  }

  // Get source manager and convert position to location
  const auto& source_manager = session->GetSourceManager();
  auto buffers = source_manager.getAllBuffers();
  if (buffers.empty()) {
    logger_->error("No buffers found in source manager for: {}", uri);
    co_return std::vector<lsp::Location>{};
  }

  // Find the buffer that matches the requested URI
  // Convert URI to path for comparison with buffer paths
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
    logger_->error(
        "No buffer found matching URI: {} (path: {})", uri,
        target_path.String());
    // Fallback to first buffer (old behavior)
    target_buffer = buffers[0];
  }

  auto location = ConvertLspPositionToSlangLocation(
      position, target_buffer, source_manager);

  // Look up definition using semantic index
  auto def_loc_opt = session->GetSemanticIndex().LookupDefinitionAt(location);
  if (!def_loc_opt) {
    logger_->debug(
        "No definition found at position {}:{} in {}", position.line,
        position.character, uri);
    co_return std::vector<lsp::Location>{};
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

  logger_->debug(
      "Found definition at {}:{}-{}:{} in {}", lsp_location.range.start.line,
      lsp_location.range.start.character, lsp_location.range.end.line,
      lsp_location.range.end.character, lsp_location.uri);

  co_return std::vector<lsp::Location>{lsp_location};
}

auto LanguageService::GetDocumentSymbols(std::string uri)
    -> asio::awaitable<std::vector<lsp::DocumentSymbol>> {
  utils::ScopedTimer timer("GetDocumentSymbols", logger_);

  if (!layout_service_) {
    logger_->error("LanguageService: Workspace not initialized");
    co_return std::vector<lsp::DocumentSymbol>{};
  }

  auto session = co_await session_manager_->GetSession(uri);
  if (!session) {
    logger_->debug("No session for {}, returning empty symbols", uri);
    co_return std::vector<lsp::DocumentSymbol>{};
  }

  co_return session->GetSemanticIndex().GetDocumentSymbols(uri);
}

auto LanguageService::HandleConfigChange() -> void {
  if (layout_service_) {
    layout_service_->RebuildLayout();
    session_manager_->InvalidateAllSessions();
    logger_->debug("LanguageService handled config change");
  }
}

auto LanguageService::HandleSourceFileChange(
    std::string uri, lsp::FileChangeType change_type) -> void {
  if (!layout_service_) {
    return;
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
      // Use lazy invalidation strategy for content changes:
      // - File saves: buffer already matches disk, cache stays valid
      // - External changes: cache miss will trigger rebuild on next LSP request
      logger_->debug("LanguageService ignoring disk content change: {}", uri);
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

  co_await session_manager_->UpdateSession(uri, content, version);
}

auto LanguageService::OnDocumentSaved(
    std::string uri, std::string content, int version)
    -> asio::awaitable<void> {
  if (!session_manager_) {
    logger_->error("LanguageService: SessionManager not initialized");
    co_return;
  }

  co_await session_manager_->UpdateSession(uri, content, version);
}

auto LanguageService::OnDocumentClosed(std::string uri) -> void {
  if (!session_manager_) {
    logger_->error("LanguageService: SessionManager not initialized");
    return;
  }

  logger_->debug("LanguageService::OnDocumentClosed: {}", uri);
  // Lazy removal: Keep session in cache for close/reopen optimization
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

}  // namespace slangd::services

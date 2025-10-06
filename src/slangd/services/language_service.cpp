#include "slangd/services/language_service.hpp"

#include <algorithm>
#include <ranges>

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

  // Get compilation state from SessionManager (waits for compilation only, not
  // indexing)
  auto state = co_await session_manager_->GetSessionForDiagnostics(uri);

  // Extract diagnostics from CompilationState (already elaborated)
  auto diagnostics = semantic::DiagnosticConverter::ExtractDiagnostics(
      state.diagnostics, *state.source_manager, state.buffer_id);
  auto filtered = semantic::DiagnosticConverter::FilterAndModifyDiagnostics(
      std::move(diagnostics));

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
    // Clear cache when layout changes (catalog version will change)
    ClearCache();
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
      ClearCache();  // Clear all cache since catalog will change
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

auto LanguageService::UpdateSession(std::string uri, std::string content)
    -> asio::awaitable<void> {
  if (!session_manager_) {
    logger_->error("LanguageService: SessionManager not initialized");
    co_return;
  }

  logger_->debug("LanguageService::UpdateSession: {}", uri);
  co_await session_manager_->UpdateSession(uri, content);
  logger_->debug("SessionManager cache updated for: {}", uri);
}

auto LanguageService::RemoveSession(std::string uri) -> void {
  if (!session_manager_) {
    logger_->error("LanguageService: SessionManager not initialized");
    return;
  }

  logger_->debug("LanguageService::RemoveSession: {}", uri);
  session_manager_->RemoveSession(uri);
  logger_->debug("SessionManager cache cleared for: {}", uri);
}

auto LanguageService::InvalidateSessions(std::vector<std::string> uris)
    -> void {
  if (!session_manager_) {
    logger_->error("LanguageService: SessionManager not initialized");
    return;
  }

  logger_->debug("LanguageService::InvalidateSessions: {} files", uris.size());
  session_manager_->InvalidateSessions(uris);
  logger_->debug("SessionManager cache invalidated for {} files", uris.size());
}

auto LanguageService::CreateOverlaySession(
    std::string uri, std::string content,
    std::shared_ptr<PendingCreation> pending)
    -> asio::awaitable<std::shared_ptr<OverlaySession>> {
  // Dispatch compilation to background thread pool
  auto session = co_await asio::co_spawn(
      compilation_pool_->get_executor(),
      [uri, content, pending,
       this]() -> asio::awaitable<std::shared_ptr<OverlaySession>> {
        utils::ScopedTimer total_timer(
            "OverlaySession two-phase creation", logger_);

        // Phase 1: Build compilation (50ms)
        utils::ScopedTimer build_timer("Compilation build", logger_);
        auto [source_manager, compilation, main_buffer_id] =
            OverlaySession::BuildCompilation(
                uri, content, layout_service_, global_catalog_, logger_);
        auto build_elapsed = build_timer.GetElapsed();
        logger_->debug(
            "Phase 1: Compilation built ({})",
            utils::ScopedTimer::FormatDuration(build_elapsed));

        // Phase 2: Elaborate with getAllDiagnostics() (76ms)
        // This FREEZES the compilation, making it safe for concurrent indexing
        utils::ScopedTimer elab_timer("Elaboration", logger_);
        auto slang_diagnostics = compilation->getAllDiagnostics();
        auto elab_elapsed = elab_timer.GetElapsed();
        logger_->debug(
            "Phase 2: Elaboration complete ({}, {} diagnostics)",
            utils::ScopedTimer::FormatDuration(elab_elapsed),
            slang_diagnostics.size());

        // Signal 1: Compilation ready for diagnostics!
        // Send copies - we keep compilation for indexing
        CompilationState state{
            .source_manager = source_manager,
            .compilation = nullptr,  // Placeholder, not sent
            .buffer_id = main_buffer_id,
            .diagnostics = slang_diagnostics  // Copy diagnostics
        };

        std::error_code ec;
        pending->compilation_ready->try_send(ec, std::move(state));
        pending->compilation_ready->close();
        logger_->debug(
            "Signal 1 sent: compilation_ready ({})",
            utils::ScopedTimer::FormatDuration(build_elapsed + elab_elapsed));

        // Phase 3: Build semantic index (455ms)
        // Compilation is now immutable, safe for indexing
        utils::ScopedTimer index_timer("Semantic indexing", logger_);
        auto semantic_index = semantic::SemanticIndex::FromCompilation(
            *compilation, *source_manager, uri, global_catalog_.get());
        auto index_elapsed = index_timer.GetElapsed();
        logger_->debug(
            "Phase 3: Semantic index built ({}, {} entries)",
            utils::ScopedTimer::FormatDuration(index_elapsed),
            semantic_index->GetSemanticEntries().size());

        // Create final session
        auto session = std::shared_ptr<OverlaySession>(new OverlaySession(
            std::move(source_manager), std::move(compilation),
            std::move(semantic_index), main_buffer_id, logger_));

        auto total_elapsed = total_timer.GetElapsed();
        logger_->debug(
            "OverlaySession created (total: {})",
            utils::ScopedTimer::FormatDuration(total_elapsed));

        co_return session;
      },
      asio::use_awaitable);

  // Post result back to main strand before returning
  co_await asio::post(executor_, asio::use_awaitable);
  co_return session;
}

auto LanguageService::GetOrCreateOverlay(
    OverlayCacheKey key, std::string content)
    -> asio::awaitable<std::shared_ptr<OverlaySession>> {
  auto now = std::chrono::steady_clock::now();

  // Check if we already have this overlay in cache
  for (auto& entry : overlay_cache_) {
    if (entry.key == key) {
      // Cache hit! Update access time and return
      entry.last_access = now;
      co_return entry.session;
    }
  }

  // Check if creation is already pending for this key
  size_t key_hash = key.Hash();
  if (auto it = pending_creations_.find(key_hash);
      it != pending_creations_.end()) {
    auto pending = it->second;
    logger_->debug(
        "Waiting for pending session creation: {}:hash{}", key.doc_uri,
        key_hash);

    // Wait for the full session to complete via session_ready channel
    std::error_code ec;
    auto result = co_await pending->session_ready->async_receive(
        asio::redirect_error(asio::use_awaitable, ec));

    if (!ec) {
      logger_->debug(
          "Received completed session from pending creation: {}:hash{}",
          key.doc_uri, key_hash);
      co_return result;
    }

    // Channel closed without sending - fall through to check cache
    logger_->debug(
        "Channel closed, checking cache: {}:hash{}", key.doc_uri, key_hash);
    for (auto& entry : overlay_cache_) {
      if (entry.key == key) {
        entry.last_access = now;
        co_return entry.session;
      }
    }
    co_return nullptr;
  }

  // Cache miss and no pending creation - we'll create it
  // Register as pending so other requests can wait
  auto pending = std::make_shared<PendingCreation>(executor_);
  pending_creations_[key_hash] = pending;
  logger_->debug(
      "Starting new overlay session creation: {}:hash{}", key.doc_uri,
      key_hash);

  auto shared_session =
      co_await CreateOverlaySession(key.doc_uri, content, pending);
  if (!shared_session) {
    logger_->error(
        "Failed to create overlay session for {}:hash{}", key.doc_uri,
        key.content_hash);
    // Signal failure and cleanup
    pending->compilation_ready->close();
    pending->session_ready->close();
    pending_creations_.erase(key_hash);
    co_return nullptr;
  }

  // Send result to all waiting coroutines
  std::error_code ec;
  pending->session_ready->try_send(ec, shared_session);
  pending->session_ready->close();  // Close channel after sending
  pending_creations_.erase(key_hash);

  // Add to cache
  CacheEntry entry{.key = key, .session = shared_session, .last_access = now};

  // If cache is full, remove oldest entry (simple LRU)
  if (overlay_cache_.size() >= kMaxCacheSize) {
    // Find oldest entry
    auto oldest_it = std::ranges::min_element(
        overlay_cache_, [](const CacheEntry& a, const CacheEntry& b) -> bool {
          return a.last_access < b.last_access;
        });

    if (oldest_it != overlay_cache_.end()) {
      logger_->debug(
          "Evicting oldest overlay from cache: {}:hash{}",
          oldest_it->key.doc_uri, oldest_it->key.content_hash);
      *oldest_it = std::move(entry);
    }
  } else {
    overlay_cache_.push_back(std::move(entry));
  }

  logger_->debug(
      "Added overlay to cache for {}:hash{} (cache size: {})", key.doc_uri,
      key.content_hash, overlay_cache_.size());
  co_return shared_session;
}

auto LanguageService::GetOrStartPendingCreation(
    OverlayCacheKey key, std::string content)
    -> std::shared_ptr<PendingCreation> {
  size_t key_hash = key.Hash();

  // Check if creation is already pending
  if (auto it = pending_creations_.find(key_hash);
      it != pending_creations_.end()) {
    logger_->debug(
        "Found existing pending creation: {}:hash{}", key.doc_uri, key_hash);
    return it->second;
  }

  // No pending creation - start new one
  auto pending = std::make_shared<PendingCreation>(executor_);
  pending_creations_[key_hash] = pending;
  logger_->debug(
      "Starting new pending creation: {}:hash{}", key.doc_uri, key_hash);

  // Start overlay session creation in background
  // Signals compilation_ready (phase 1) and session_ready (phase 2)
  asio::co_spawn(
      executor_,
      [this, key, content, pending]() -> asio::awaitable<void> {
        auto session =
            co_await CreateOverlaySession(key.doc_uri, content, pending);
        if (session) {
          // Signal session_ready
          std::error_code ec;
          pending->session_ready->try_send(ec, session);
          pending->session_ready->close();

          // Add to cache
          auto now = std::chrono::steady_clock::now();
          CacheEntry entry{.key = key, .session = session, .last_access = now};

          if (overlay_cache_.size() >= kMaxCacheSize) {
            auto oldest_it = std::ranges::min_element(
                overlay_cache_,
                [](const CacheEntry& a, const CacheEntry& b) -> bool {
                  return a.last_access < b.last_access;
                });
            if (oldest_it != overlay_cache_.end()) {
              logger_->debug(
                  "Evicting oldest overlay from cache: {}:hash{}",
                  oldest_it->key.doc_uri, oldest_it->key.content_hash);
              *oldest_it = std::move(entry);
            }
          } else {
            overlay_cache_.push_back(std::move(entry));
          }

          logger_->debug(
              "Added session to cache: {}:hash{} (size: {})", key.doc_uri,
              key.Hash(), overlay_cache_.size());
        } else {
          // Signal failure
          pending->session_ready->close();
          logger_->error(
              "Failed to create session: {}:hash{}", key.doc_uri, key.Hash());
        }
        pending_creations_.erase(key.Hash());
      },
      asio::detached);

  return pending;
}

auto LanguageService::ClearCache() -> void {
  logger_->debug("Clearing overlay cache ({} entries)", overlay_cache_.size());
  overlay_cache_.clear();
}

auto LanguageService::ClearCacheForFile(const std::string& uri) -> void {
  auto it = std::ranges::remove_if(
      overlay_cache_, [&uri](const CacheEntry& entry) -> bool {
        return entry.key.doc_uri == uri;
      });

  size_t removed_count = std::distance(it.begin(), overlay_cache_.end());
  overlay_cache_.erase(it.begin(), overlay_cache_.end());

  logger_->debug(
      "Cleared {} overlay cache entries for file: {}", removed_count, uri);
}

}  // namespace slangd::services

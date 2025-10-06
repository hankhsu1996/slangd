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

  // Create project layout service
  auto workspace_path = CanonicalPath::FromUri(workspace_uri);
  layout_service_ =
      ProjectLayoutService::Create(executor_, workspace_path, logger_);
  co_await layout_service_->LoadConfig(workspace_path);

  // Phase 2: Create GlobalCatalog from ProjectLayoutService
  global_catalog_ =
      GlobalCatalog::CreateFromProjectLayout(layout_service_, logger_);

  if (global_catalog_) {
    logger_->debug(
        "LanguageService created GlobalCatalog with {} packages, version {}",
        global_catalog_->GetPackages().size(), global_catalog_->GetVersion());
  } else {
    logger_->error("LanguageService failed to create GlobalCatalog");
  }

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

auto LanguageService::ComputeDiagnostics(std::string uri, std::string content)
    -> asio::awaitable<std::vector<lsp::Diagnostic>> {
  utils::ScopedTimer timer("ComputeDiagnostics", logger_);

  if (!layout_service_) {
    logger_->error("LanguageService: Workspace not initialized");
    co_return std::vector<lsp::Diagnostic>{};
  }

  logger_->debug("LanguageService computing full diagnostics for: {}", uri);

  // Create cache key with catalog version and content hash
  uint64_t catalog_version =
      global_catalog_ ? global_catalog_->GetVersion() : 0;
  size_t content_hash = std::hash<std::string>{}(content);
  OverlayCacheKey cache_key{
      .doc_uri = uri,
      .content_hash = content_hash,
      .catalog_version = catalog_version};

  // Get or create overlay session from cache
  auto session = co_await GetOrCreateOverlay(cache_key, content);
  if (!session) {
    logger_->error("Failed to create overlay session for: {}", uri);
    co_return std::vector<lsp::Diagnostic>{};
  }

  // Extract all diagnostics in background thread pool (triggers elaboration)
  auto diagnostics = co_await asio::co_spawn(
      compilation_pool_->get_executor(),
      [session, uri, this]() -> asio::awaitable<std::vector<lsp::Diagnostic>> {
        co_return semantic::DiagnosticConverter::ExtractAllDiagnostics(
            session->GetCompilation(), session->GetSourceManager(),
            session->GetMainBufferID(), logger_);
      },
      asio::use_awaitable);

  // Post result back to main strand
  co_await asio::post(executor_, asio::use_awaitable);

  logger_->debug(
      "LanguageService computed {} full diagnostics for: {}",
      diagnostics.size(), uri);

  co_return diagnostics;
}

auto LanguageService::GetDefinitionsForPosition(
    std::string uri, lsp::Position position, std::string content)
    -> asio::awaitable<std::vector<lsp::Location>> {
  utils::ScopedTimer timer("GetDefinitionsForPosition", logger_);

  if (!layout_service_) {
    logger_->error("LanguageService: Workspace not initialized");
    co_return std::vector<lsp::Location>{};
  }

  logger_->debug(
      "LanguageService getting definitions for: {} at {}:{}", uri,
      position.line, position.character);

  // Create cache key with catalog version and content hash
  uint64_t catalog_version =
      global_catalog_ ? global_catalog_->GetVersion() : 0;
  size_t content_hash = std::hash<std::string>{}(content);
  OverlayCacheKey cache_key{
      .doc_uri = uri,
      .content_hash = content_hash,
      .catalog_version = catalog_version};

  // Get or create overlay session from cache
  auto session = co_await GetOrCreateOverlay(cache_key, content);

  if (!session) {
    logger_->error("Failed to create overlay session for: {}", uri);
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

auto LanguageService::GetDocumentSymbols(std::string uri, std::string content)
    -> asio::awaitable<std::vector<lsp::DocumentSymbol>> {
  utils::ScopedTimer timer("GetDocumentSymbols", logger_);

  if (!layout_service_) {
    logger_->error("LanguageService: Workspace not initialized");
    co_return std::vector<lsp::DocumentSymbol>{};
  }

  logger_->debug("LanguageService getting document symbols for: {}", uri);

  // Create cache key with catalog version and content hash
  uint64_t catalog_version =
      global_catalog_ ? global_catalog_->GetVersion() : 0;
  size_t content_hash = std::hash<std::string>{}(content);
  OverlayCacheKey cache_key{
      .doc_uri = uri,
      .content_hash = content_hash,
      .catalog_version = catalog_version};

  // Get or create overlay session from cache
  auto overlay_start = std::chrono::steady_clock::now();
  auto session = co_await GetOrCreateOverlay(cache_key, content);
  auto overlay_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - overlay_start);
  logger_->debug(
      "GetOrCreateOverlay completed ({})",
      utils::ScopedTimer::FormatDuration(overlay_elapsed));

  if (!session) {
    logger_->error("Failed to create overlay session for: {}", uri);
    co_return std::vector<lsp::DocumentSymbol>{};
  }

  // Use the unified SemanticIndex for document symbols
  auto symbols_start = std::chrono::steady_clock::now();
  auto symbols = session->GetSemanticIndex().GetDocumentSymbols(uri);
  auto symbols_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - symbols_start);
  logger_->debug(
      "GetDocumentSymbols from index completed ({})",
      utils::ScopedTimer::FormatDuration(symbols_elapsed));

  co_return symbols;
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

auto LanguageService::CreateOverlaySession(std::string uri, std::string content)
    -> asio::awaitable<std::shared_ptr<OverlaySession>> {
  // Dispatch compilation to background thread pool
  auto session = co_await asio::co_spawn(
      compilation_pool_->get_executor(),
      [uri, content,
       this]() -> asio::awaitable<std::shared_ptr<OverlaySession>> {
        // SystemVerilog compilation runs on background thread
        co_return OverlaySession::Create(
            uri, content, layout_service_, global_catalog_, logger_);
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

    // Wait for the pending creation to complete via channel
    std::error_code ec;
    auto result = co_await pending->channel->async_receive(
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

  auto shared_session = co_await CreateOverlaySession(key.doc_uri, content);
  if (!shared_session) {
    logger_->error(
        "Failed to create overlay session for {}:hash{}", key.doc_uri,
        key.content_hash);
    // Signal failure and cleanup
    pending->channel->close();
    pending_creations_.erase(key_hash);
    co_return nullptr;
  }

  // Send result to all waiting coroutines
  std::error_code ec;
  pending->channel->try_send(ec, shared_session);
  pending->channel->close();  // Close channel after sending
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

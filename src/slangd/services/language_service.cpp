#include "slangd/services/language_service.hpp"

#include <algorithm>

#include <slang/ast/symbols/CompilationUnitSymbols.h>
#include <slang/diagnostics/DiagnosticEngine.h>
#include <slang/syntax/SyntaxTree.h>

#include "slangd/services/global_catalog.hpp"
#include "slangd/utils/canonical_path.hpp"
#include "slangd/utils/conversion.hpp"
#include "slangd/utils/scoped_timer.hpp"

namespace slangd::services {

LanguageService::LanguageService(
    asio::any_io_executor executor, std::shared_ptr<spdlog::logger> logger)
    : global_catalog_(nullptr),
      logger_(logger ? logger : spdlog::default_logger()),
      executor_(std::move(executor)) {
  logger_->debug("LanguageService created");
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

auto LanguageService::ComputeDiagnostics(
    std::string uri, std::string content, int version)
    -> asio::awaitable<std::vector<lsp::Diagnostic>> {
  if (!layout_service_) {
    logger_->error("LanguageService: Workspace not initialized");
    co_return std::vector<lsp::Diagnostic>{};
  }

  logger_->debug("LanguageService computing diagnostics for: {}", uri);

  // Create cache key with catalog version
  uint64_t catalog_version =
      global_catalog_ ? global_catalog_->GetVersion() : 0;
  OverlayCacheKey cache_key{
      .doc_uri = uri,
      .doc_version = version,
      .catalog_version = catalog_version};

  // Get or create overlay session from cache
  auto session = GetOrCreateOverlay(cache_key, content);
  if (!session) {
    logger_->error("Failed to create overlay session for: {}", uri);
    co_return std::vector<lsp::Diagnostic>{};
  }

  // Get diagnostics directly from DiagnosticIndex - consistent pattern!
  const auto& diagnostics = session->GetDiagnosticIndex().GetDiagnostics();

  logger_->debug(
      "LanguageService computed {} diagnostics for: {}", diagnostics.size(),
      uri);

  co_return diagnostics;
}

auto LanguageService::GetDefinitionsForPosition(
    std::string uri, lsp::Position position, std::string content, int version)
    -> std::vector<lsp::Location> {
  if (!layout_service_) {
    logger_->error("LanguageService: Workspace not initialized");
    return {};
  }

  logger_->debug(
      "LanguageService getting definitions for: {} at {}:{}", uri,
      position.line, position.character);

  // Create cache key with catalog version
  uint64_t catalog_version =
      global_catalog_ ? global_catalog_->GetVersion() : 0;
  OverlayCacheKey cache_key{
      .doc_uri = uri,
      .doc_version = version,
      .catalog_version = catalog_version};

  // Get or create overlay session from cache
  auto session = GetOrCreateOverlay(cache_key, content);
  if (!session) {
    logger_->error("Failed to create overlay session for: {}", uri);
    return {};
  }

  // Get source manager and convert position to location
  const auto& source_manager = session->GetSourceManager();
  auto buffers = source_manager.getAllBuffers();
  if (buffers.empty()) {
    logger_->error("No buffers found in source manager for: {}", uri);
    return {};
  }
  auto buffer = buffers[0];
  auto location =
      ConvertLspPositionToSlangLocation(position, buffer, source_manager);

  // Use DefinitionIndex directly - this is the new architecture!
  auto symbol_key = session->GetDefinitionIndex().LookupSymbolAt(location);
  if (!symbol_key) {
    logger_->debug(
        "No symbol found at position {}:{} in {}", position.line,
        position.character, uri);
    return {};
  }

  auto def_range_opt =
      session->GetDefinitionIndex().GetDefinitionRange(*symbol_key);
  if (!def_range_opt) {
    logger_->debug(
        "Definition location not found for symbol at {}:{} in {}",
        position.line, position.character, uri);
    return {};
  }

  // Convert to LSP location with correct file URI
  auto lsp_location =
      ConvertSlangLocationToLspLocation(def_range_opt->start(), source_manager);
  // Update range to use the full definition range
  lsp_location.range =
      ConvertSlangRangeToLspRange(*def_range_opt, source_manager);

  logger_->debug(
      "Found definition at {}:{}-{}:{} in {}", lsp_location.range.start.line,
      lsp_location.range.start.character, lsp_location.range.end.line,
      lsp_location.range.end.character, uri);

  return {lsp_location};
}

auto LanguageService::GetDocumentSymbols(
    std::string uri, std::string content, int version)
    -> std::vector<lsp::DocumentSymbol> {
  if (!layout_service_) {
    logger_->error("LanguageService: Workspace not initialized");
    return {};
  }

  logger_->debug("LanguageService getting document symbols for: {}", uri);

  // Create cache key with catalog version
  uint64_t catalog_version =
      global_catalog_ ? global_catalog_->GetVersion() : 0;
  OverlayCacheKey cache_key{
      .doc_uri = uri,
      .doc_version = version,
      .catalog_version = catalog_version};

  // Get or create overlay session from cache
  auto session = GetOrCreateOverlay(cache_key, content);
  if (!session) {
    logger_->error("Failed to create overlay session for: {}", uri);
    return {};
  }

  // Use the new SymbolIndex for clean delegation
  return session->GetSymbolIndex().GetDocumentSymbols(uri);
}

auto LanguageService::HandleConfigChange() -> void {
  if (layout_service_) {
    layout_service_->RebuildLayout();
    // Clear cache when layout changes (catalog version will change)
    ClearCache();
    logger_->debug("LanguageService handled config change");
  }
}

auto LanguageService::HandleSourceFileChange() -> void {
  if (layout_service_) {
    layout_service_->ScheduleDebouncedRebuild();
    // Clear cache when layout changes (catalog version will change)
    ClearCache();
    logger_->debug("LanguageService handled source file change");
  }
}

auto LanguageService::CreateOverlaySession(std::string uri, std::string content)
    -> std::shared_ptr<OverlaySession> {
  return OverlaySession::Create(
      uri, content, layout_service_, global_catalog_, logger_);
}

auto LanguageService::GetOrCreateOverlay(
    const OverlayCacheKey& key, const std::string& content)
    -> std::shared_ptr<OverlaySession> {
  auto now = std::chrono::steady_clock::now();

  // Check if we already have this overlay in cache
  for (auto& entry : overlay_cache_) {
    if (entry.key == key) {
      // Cache hit! Update access time and return
      entry.last_access = now;
      return entry.session;
    }
  }

  // Cache miss - create new overlay session

  auto shared_session = CreateOverlaySession(key.doc_uri, content);
  if (!shared_session) {
    logger_->error(
        "Failed to create overlay session for {}:v{}", key.doc_uri,
        key.doc_version);
    return nullptr;
  }

  // Add to cache
  CacheEntry entry{.key = key, .session = shared_session, .last_access = now};

  // If cache is full, remove oldest entry (simple LRU)
  if (overlay_cache_.size() >= kMaxCacheSize) {
    // Find oldest entry
    auto oldest_it = std::min_element(
        overlay_cache_.begin(), overlay_cache_.end(),
        [](const CacheEntry& a, const CacheEntry& b) {
          return a.last_access < b.last_access;
        });

    if (oldest_it != overlay_cache_.end()) {
      logger_->debug(
          "Evicting oldest overlay from cache: {}:v{}", oldest_it->key.doc_uri,
          oldest_it->key.doc_version);
      *oldest_it = std::move(entry);
    }
  } else {
    overlay_cache_.push_back(std::move(entry));
  }

  logger_->debug(
      "Added overlay to cache for {}:v{} (cache size: {})", key.doc_uri,
      key.doc_version, overlay_cache_.size());
  return shared_session;
}

auto LanguageService::ClearCache() -> void {
  logger_->debug("Clearing overlay cache ({} entries)", overlay_cache_.size());
  overlay_cache_.clear();
}

}  // namespace slangd::services

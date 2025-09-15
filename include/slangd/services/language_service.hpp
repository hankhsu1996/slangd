#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <asio.hpp>
#include <spdlog/spdlog.h>

#include "slangd/core/language_service_base.hpp"
#include "slangd/core/project_layout_service.hpp"
#include "slangd/services/global_catalog.hpp"
#include "slangd/services/overlay_session.hpp"

namespace slangd::services {

// Cache key for overlay sessions
struct OverlayCacheKey {
  std::string doc_uri;
  int doc_version;
  uint64_t catalog_version;

  auto operator==(const OverlayCacheKey& other) const -> bool {
    return doc_uri == other.doc_uri && doc_version == other.doc_version &&
           catalog_version == other.catalog_version;
  }

  [[nodiscard]] auto Hash() const -> size_t {
    size_t h1 = std::hash<std::string>{}(doc_uri);
    size_t h2 = std::hash<int>{}(doc_version);
    size_t h3 = std::hash<uint64_t>{}(catalog_version);

    // Combine hashes using boost-style hash combination
    size_t result = h1;
    result ^= h2 + 0x9e3779b9 + (result << 6) + (result >> 2);
    result ^= h3 + 0x9e3779b9 + (result << 6) + (result >> 2);
    return result;
  }
};

// New service implementation using overlay sessions
// Creates fresh Compilation + DefinitionIndex per LSP request
// Designed for GlobalCatalog integration (Phase 2)
class LanguageService : public LanguageServiceBase {
 public:
  // Constructor for late initialization (workspace set up later)
  explicit LanguageService(
      asio::any_io_executor executor,
      std::shared_ptr<spdlog::logger> logger = nullptr);

  // Initialize with workspace folder (called during LSP initialize)
  // Initialize workspace for LSP operations
  auto InitializeWorkspace(std::string workspace_uri)
      -> asio::awaitable<void> override;

  // LanguageServiceBase implementation using overlay sessions
  auto ComputeDiagnostics(std::string uri, std::string content, int version)
      -> asio::awaitable<std::vector<lsp::Diagnostic>> override;

  auto GetDefinitionsForPosition(
      std::string uri, lsp::Position position, std::string content, int version)
      -> std::vector<lsp::Location> override;

  auto GetDocumentSymbols(std::string uri, std::string content, int version)
      -> std::vector<lsp::DocumentSymbol> override;

  auto HandleConfigChange() -> void override;

  auto HandleSourceFileChange() -> void override;

 private:
  // Cache entry for overlay sessions
  struct CacheEntry {
    OverlayCacheKey key;
    std::shared_ptr<OverlaySession> session;
    std::chrono::steady_clock::time_point last_access;
  };

  // Create overlay session for the given URI and content
  auto CreateOverlaySession(std::string uri, std::string content)
      -> std::shared_ptr<OverlaySession>;

  // Get or create overlay session from cache
  auto GetOrCreateOverlay(
      const OverlayCacheKey& key, const std::string& content)
      -> std::shared_ptr<OverlaySession>;

  // Clear cache when catalog version changes
  auto ClearCache() -> void;

  // Core dependencies
  std::shared_ptr<ProjectLayoutService> layout_service_;
  std::shared_ptr<const GlobalCatalog> global_catalog_;
  std::shared_ptr<spdlog::logger> logger_;
  asio::any_io_executor executor_;

  // LRU cache for overlay sessions
  std::vector<CacheEntry> overlay_cache_;
  static constexpr size_t kMaxCacheSize = 8;
};

}  // namespace slangd::services

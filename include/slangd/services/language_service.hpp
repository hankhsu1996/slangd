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
  size_t content_hash;
  uint64_t catalog_version;

  auto operator==(const OverlayCacheKey& other) const -> bool {
    return doc_uri == other.doc_uri && content_hash == other.content_hash &&
           catalog_version == other.catalog_version;
  }

  [[nodiscard]] auto Hash() const -> size_t {
    size_t h1 = std::hash<std::string>{}(doc_uri);
    size_t h2 = content_hash;
    size_t h3 = std::hash<uint64_t>{}(catalog_version);

    // Combine hashes using boost-style hash combination
    size_t result = h1;
    result ^= h2 + 0x9e3779b9 + (result << 6) + (result >> 2);
    result ^= h3 + 0x9e3779b9 + (result << 6) + (result >> 2);
    return result;
  }
};

// Service implementation using overlay sessions
// Creates fresh Compilation + SemanticIndex per LSP request
// Supports GlobalCatalog integration for cross-file functionality
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
  auto ComputeDiagnostics(std::string uri, std::string content)
      -> asio::awaitable<std::vector<lsp::Diagnostic>> override;

  auto GetDefinitionsForPosition(
      std::string uri, lsp::Position position, std::string content)
      -> asio::awaitable<std::vector<lsp::Location>> override;

  auto GetDocumentSymbols(std::string uri, std::string content)
      -> asio::awaitable<std::vector<lsp::DocumentSymbol>> override;

  auto HandleConfigChange() -> void override;

  auto HandleSourceFileChange(std::string uri, lsp::FileChangeType change_type)
      -> void override;

 private:
  // Cache entry for overlay sessions
  struct CacheEntry {
    OverlayCacheKey key;
    std::shared_ptr<OverlaySession> session;
    std::chrono::steady_clock::time_point last_access;
  };

  // Create overlay session for the given URI and content
  auto CreateOverlaySession(std::string uri, std::string content)
      -> asio::awaitable<std::shared_ptr<OverlaySession>>;

  // Get or create overlay session from cache
  auto GetOrCreateOverlay(OverlayCacheKey key, std::string content)
      -> asio::awaitable<std::shared_ptr<OverlaySession>>;

  // Clear cache when catalog version changes
  auto ClearCache() -> void;

  // Clear cache entries for specific file
  auto ClearCacheForFile(const std::string& uri) -> void;

  // Core dependencies
  std::shared_ptr<ProjectLayoutService> layout_service_;
  std::shared_ptr<const GlobalCatalog> global_catalog_;
  std::shared_ptr<spdlog::logger> logger_;
  asio::any_io_executor executor_;

  // LRU cache for overlay sessions
  std::vector<CacheEntry> overlay_cache_;
  static constexpr size_t kMaxCacheSize = 16;

  // Background thread pool for overlay compilation
  std::unique_ptr<asio::thread_pool> compilation_pool_;
  static constexpr size_t kThreadPoolSize = 4;
};

}  // namespace slangd::services

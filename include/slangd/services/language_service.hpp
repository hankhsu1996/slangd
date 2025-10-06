#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <asio.hpp>
#include <asio/experimental/channel.hpp>
#include <spdlog/spdlog.h>

#include "slangd/core/language_service_base.hpp"
#include "slangd/core/project_layout_service.hpp"
#include "slangd/services/global_catalog.hpp"
#include "slangd/services/overlay_session.hpp"
#include "slangd/services/session_manager.hpp"

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

  // LanguageServiceBase implementation
  auto InitializeWorkspace(std::string workspace_uri)
      -> asio::awaitable<void> override;

  auto ComputeParseDiagnostics(std::string uri, std::string content)
      -> asio::awaitable<std::vector<lsp::Diagnostic>> override;

  auto ComputeDiagnostics(std::string uri)
      -> asio::awaitable<std::vector<lsp::Diagnostic>> override;

  auto GetDefinitionsForPosition(
      std::string uri, lsp::Position position, std::string content)
      -> asio::awaitable<std::vector<lsp::Location>> override;

  auto GetDocumentSymbols(std::string uri)
      -> asio::awaitable<std::vector<lsp::DocumentSymbol>> override;

  auto HandleConfigChange() -> void override;

  auto HandleSourceFileChange(std::string uri, lsp::FileChangeType change_type)
      -> void override;

  // Session lifecycle management
  auto UpdateSession(std::string uri, std::string content)
      -> asio::awaitable<void> override;

  auto RemoveSession(std::string uri) -> void override;

  auto InvalidateSessions(std::vector<std::string> uris) -> void override;

 private:
  // Cache entry for overlay sessions
  struct CacheEntry {
    OverlayCacheKey key;
    std::shared_ptr<OverlaySession> session;
    std::chrono::steady_clock::time_point last_access;
  };

  // Compilation state for two-phase session creation
  // Phase 1: Compilation + elaboration (fast - for diagnostics)
  // Phase 2: Semantic indexing (slow - for go-to-def/symbols)
  struct CompilationState {
    std::shared_ptr<slang::SourceManager> source_manager;
    std::unique_ptr<slang::ast::Compilation> compilation;
    slang::BufferID buffer_id;
    slang::Diagnostics diagnostics;
  };

  // Pending session creation - allows concurrent requests to wait for
  // completion at different phases
  struct PendingCreation {
    using CompilationChannel =
        asio::experimental::channel<void(std::error_code, CompilationState)>;
    using SessionChannel = asio::experimental::channel<void(
        std::error_code, std::shared_ptr<OverlaySession>)>;

    // Signal 1: Compilation ready (after elaboration, before indexing)
    std::shared_ptr<CompilationChannel> compilation_ready;
    // Signal 2: Full session ready (after indexing)
    std::shared_ptr<SessionChannel> session_ready;

    explicit PendingCreation(asio::any_io_executor executor)
        : compilation_ready(std::make_shared<CompilationChannel>(executor, 10)),
          session_ready(std::make_shared<SessionChannel>(executor, 10)) {
    }
  };

  // Create overlay session for the given URI and content
  // Signals compilation_ready after elaboration, session_ready after indexing
  auto CreateOverlaySession(
      std::string uri, std::string content,
      std::shared_ptr<PendingCreation> pending)
      -> asio::awaitable<std::shared_ptr<OverlaySession>>;

  // Get or create overlay session from cache
  auto GetOrCreateOverlay(OverlayCacheKey key, std::string content)
      -> asio::awaitable<std::shared_ptr<OverlaySession>>;

  // Get existing or start new pending creation (for two-phase access)
  auto GetOrStartPendingCreation(OverlayCacheKey key, std::string content)
      -> std::shared_ptr<PendingCreation>;

  // Clear cache when catalog version changes
  auto ClearCache() -> void;

  // Clear cache entries for specific file
  auto ClearCacheForFile(const std::string& uri) -> void;

  // Core dependencies
  std::shared_ptr<ProjectLayoutService> layout_service_;
  std::shared_ptr<const GlobalCatalog> global_catalog_;
  std::shared_ptr<spdlog::logger> logger_;
  asio::any_io_executor executor_;

  std::unique_ptr<SessionManager> session_manager_;

  // LRU cache for overlay sessions
  std::vector<CacheEntry> overlay_cache_;
  static constexpr size_t kMaxCacheSize = 16;

  // Track pending overlay session creations to avoid duplicate work
  std::unordered_map<size_t, std::shared_ptr<PendingCreation>>
      pending_creations_;

  // Background thread pool for overlay compilation
  std::unique_ptr<asio::thread_pool> compilation_pool_;
  static constexpr size_t kThreadPoolSize = 4;
};

}  // namespace slangd::services

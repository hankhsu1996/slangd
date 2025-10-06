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

// Service implementation using SessionManager for lifecycle management
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

  auto GetDefinitionsForPosition(std::string uri, lsp::Position position)
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
  // Core dependencies
  std::shared_ptr<ProjectLayoutService> layout_service_;
  std::shared_ptr<const GlobalCatalog> global_catalog_;
  std::shared_ptr<spdlog::logger> logger_;
  asio::any_io_executor executor_;

  std::unique_ptr<SessionManager> session_manager_;

  // Background thread pool for parse diagnostics
  std::unique_ptr<asio::thread_pool> compilation_pool_;
  static constexpr size_t kThreadPoolSize = 4;
};

}  // namespace slangd::services

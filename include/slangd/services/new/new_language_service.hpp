#pragma once

#include <memory>

#include <asio.hpp>
#include <spdlog/spdlog.h>

#include "slangd/core/global_catalog.hpp"
#include "slangd/core/language_service_base.hpp"
#include "slangd/core/project_layout_service.hpp"
#include "slangd/services/new/overlay_session.hpp"

namespace slangd::services::new_service {

// New service implementation using overlay sessions
// Creates fresh Compilation + SymbolIndex per LSP request
// Designed for GlobalCatalog integration (Phase 2)
class NewLanguageService : public LanguageServiceBase {
 public:
  // Constructor for late initialization (workspace set up later)
  explicit NewLanguageService(
      asio::any_io_executor executor,
      std::shared_ptr<spdlog::logger> logger = nullptr);

  // Initialize with workspace folder (called during LSP initialize)
  // Same pattern as LegacyLanguageService for compatibility
  auto InitializeWorkspace(std::string workspace_uri) -> asio::awaitable<void>;

  // LanguageServiceBase implementation using overlay sessions
  auto ComputeDiagnostics(std::string uri, std::string content)
      -> asio::awaitable<std::vector<lsp::Diagnostic>> override;

  auto GetDefinitionsForPosition(std::string uri, lsp::Position position)
      -> std::vector<lsp::Location> override;

  auto GetDocumentSymbols(std::string uri)
      -> std::vector<lsp::DocumentSymbol> override;

  auto HandleConfigChange() -> void override;

  auto HandleSourceFileChange() -> void override;

 private:
  // Create overlay session for the given URI and content
  auto CreateOverlaySession(std::string uri, std::string content)
      -> std::unique_ptr<overlay::OverlaySession>;

  // Convert Slang diagnostics to LSP diagnostics
  auto ConvertSlangDiagnosticsToLsp(
      const slang::ast::Compilation& compilation,
      const slang::SourceManager& source_manager, const std::string& uri)
      -> std::vector<lsp::Diagnostic>;

  // Convert SymbolIndex results to LSP locations
  auto ConvertSymbolIndexToLspLocations(
      const semantic::SymbolIndex& symbol_index,
      const slang::SourceManager& source_manager,
      const semantic::SymbolKey& symbol_key) -> std::vector<lsp::Location>;

  // Extract document symbols from SymbolIndex
  auto ExtractDocumentSymbolsFromIndex(
      const semantic::SymbolIndex& symbol_index,
      const slang::SourceManager& source_manager, const std::string& uri)
      -> std::vector<lsp::DocumentSymbol>;

  // Core dependencies
  std::shared_ptr<ProjectLayoutService> layout_service_;
  std::shared_ptr<const GlobalCatalog> global_catalog_;  // nullptr for Phase 1a
  std::shared_ptr<spdlog::logger> logger_;
  asio::any_io_executor executor_;
};

}  // namespace slangd::services::new_service

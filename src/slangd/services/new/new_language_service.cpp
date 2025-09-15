#include "slangd/services/new/new_language_service.hpp"

#include <slang/diagnostics/DiagnosticEngine.h>
#include <slang/syntax/SyntaxTree.h>

#include "slangd/services/legacy/diagnostics_provider.hpp"
#include "slangd/utils/canonical_path.hpp"

namespace slangd::services::new_service {

NewLanguageService::NewLanguageService(
    asio::any_io_executor executor, std::shared_ptr<spdlog::logger> logger)
    : global_catalog_(nullptr),  // Phase 1a: no catalog yet
      logger_(logger ? logger : spdlog::default_logger()),
      executor_(std::move(executor)) {
  logger_->debug("NewLanguageService created");
}

auto NewLanguageService::InitializeWorkspace(std::string workspace_uri)
    -> asio::awaitable<void> {
  logger_->debug(
      "NewLanguageService initializing workspace: {}", workspace_uri);

  // Create project layout service
  auto workspace_path = CanonicalPath::FromUri(workspace_uri);
  layout_service_ =
      ProjectLayoutService::Create(executor_, workspace_path, logger_);
  co_await layout_service_->LoadConfig(workspace_path);

  // Phase 1a: global_catalog_ remains nullptr
  // Phase 2 will initialize GlobalCatalog here

  logger_->debug("NewLanguageService workspace initialized: {}", workspace_uri);
}

auto NewLanguageService::ComputeDiagnostics(
    std::string uri, std::string content)
    -> asio::awaitable<std::vector<lsp::Diagnostic>> {
  if (!layout_service_) {
    logger_->error("NewLanguageService: Workspace not initialized");
    co_return std::vector<lsp::Diagnostic>{};
  }

  logger_->debug("NewLanguageService computing diagnostics for: {}", uri);

  // Create overlay session for this request
  auto session = CreateOverlaySession(uri, content);
  if (!session) {
    logger_->error("Failed to create overlay session for: {}", uri);
    co_return std::vector<lsp::Diagnostic>{};
  }

  // Convert Slang diagnostics to LSP format
  auto diagnostics = ConvertSlangDiagnosticsToLsp(
      session->GetCompilation(), session->GetSourceManager(), uri);

  logger_->debug(
      "NewLanguageService computed {} diagnostics for: {}", diagnostics.size(),
      uri);

  co_return diagnostics;
}

auto NewLanguageService::GetDefinitionsForPosition(
    std::string uri, lsp::Position position) -> std::vector<lsp::Location> {
  if (!layout_service_) {
    logger_->error("NewLanguageService: Workspace not initialized");
    return {};
  }

  logger_->debug(
      "NewLanguageService getting definitions for: {} at {}:{}", uri,
      position.line, position.character);

  // Phase 1a: Basic stub - will be implemented in Phase 1b
  // TODO(hankhsu): Create overlay session and use SymbolIndex for lookups
  logger_->debug("GetDefinitionsForPosition: Implementation pending Phase 1b");
  return {};
}

auto NewLanguageService::GetDocumentSymbols(std::string uri)
    -> std::vector<lsp::DocumentSymbol> {
  if (!layout_service_) {
    logger_->error("NewLanguageService: Workspace not initialized");
    return {};
  }

  logger_->debug("NewLanguageService getting document symbols for: {}", uri);

  // Phase 1a: Basic stub - will be implemented in Phase 1b
  // TODO(hankhsu): Create overlay session and extract symbols from SymbolIndex
  logger_->debug("GetDocumentSymbols: Implementation pending Phase 1b");
  return {};
}

auto NewLanguageService::HandleConfigChange() -> void {
  if (layout_service_) {
    layout_service_->RebuildLayout();
    logger_->debug("NewLanguageService handled config change");
  }
}

auto NewLanguageService::HandleSourceFileChange() -> void {
  if (layout_service_) {
    layout_service_->ScheduleDebouncedRebuild();
    logger_->debug("NewLanguageService handled source file change");
  }
}

auto NewLanguageService::CreateOverlaySession(
    std::string uri, std::string content)
    -> std::unique_ptr<overlay::OverlaySession> {
  return overlay::OverlaySession::Create(
      uri, content, layout_service_, global_catalog_, logger_);
}

auto NewLanguageService::ConvertSlangDiagnosticsToLsp(
    const slang::ast::Compilation& compilation,
    const slang::SourceManager& source_manager, const std::string& uri)
    -> std::vector<lsp::Diagnostic> {
  // Phase 1a: Stub implementation - basic diagnostics only
  // Will be properly implemented in Phase 1b using existing DiagnosticsProvider

  // For now, return basic syntax diagnostics by using the compilation
  // Create shared pointers for compatibility with existing code
  auto compilation_ptr = std::shared_ptr<slang::ast::Compilation>(
      const_cast<slang::ast::Compilation*>(&compilation), [](auto*) {});
  auto source_manager_ptr = std::shared_ptr<slang::SourceManager>(
      const_cast<slang::SourceManager*>(&source_manager), [](auto*) {});

  // Use existing static method from DiagnosticsProvider
  return DiagnosticsProvider::ResolveDiagnosticsFromCompilation(
      compilation_ptr, nullptr, source_manager_ptr, uri);
}

auto NewLanguageService::ConvertSymbolIndexToLspLocations(
    const semantic::SymbolIndex& symbol_index,
    const slang::SourceManager& source_manager,
    const semantic::SymbolKey& symbol_key) -> std::vector<lsp::Location> {
  // Phase 1a: Stub - will be implemented in Phase 1b
  logger_->debug(
      "ConvertSymbolIndexToLspLocations: Implementation pending Phase 1b");
  return {};
}

auto NewLanguageService::ExtractDocumentSymbolsFromIndex(
    const semantic::SymbolIndex& symbol_index,
    const slang::SourceManager& source_manager, const std::string& uri)
    -> std::vector<lsp::DocumentSymbol> {
  // Phase 1a: Stub - will be implemented in Phase 1b
  logger_->debug(
      "ExtractDocumentSymbolsFromIndex: Implementation pending Phase 1b");
  return {};
}

}  // namespace slangd::services::new_service

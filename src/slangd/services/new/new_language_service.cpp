#include "slangd/services/new/new_language_service.hpp"

#include <slang/ast/symbols/CompilationUnitSymbols.h>
#include <slang/diagnostics/DiagnosticEngine.h>
#include <slang/syntax/SyntaxTree.h>

#include "slangd/utils/canonical_path.hpp"
#include "slangd/utils/conversion.hpp"

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

  // Get diagnostics directly from DiagnosticIndex - consistent pattern!
  const auto& diagnostics = session->GetDiagnosticIndex().GetDiagnostics();

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

  // Create overlay session for this request
  // Note: We need the current buffer content, but since this is sync method,
  // we'll use empty content for now (this should be improved in the future)
  auto session = CreateOverlaySession(uri, "");
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

  // Convert to LSP location
  auto lsp_range = ConvertSlangRangeToLspRange(*def_range_opt, source_manager);
  lsp::Location lsp_location;
  lsp_location.uri = uri;
  lsp_location.range = lsp_range;

  logger_->debug(
      "Found definition at {}:{}-{}:{} in {}", lsp_location.range.start.line,
      lsp_location.range.start.character, lsp_location.range.end.line,
      lsp_location.range.end.character, uri);

  return {lsp_location};
}

auto NewLanguageService::GetDocumentSymbols(std::string uri)
    -> std::vector<lsp::DocumentSymbol> {
  if (!layout_service_) {
    logger_->error("NewLanguageService: Workspace not initialized");
    return {};
  }

  logger_->debug("NewLanguageService getting document symbols for: {}", uri);

  // Create overlay session for this request
  auto session = CreateOverlaySession(uri, "");
  if (!session) {
    logger_->error("Failed to create overlay session for: {}", uri);
    return {};
  }

  // Use the new SymbolIndex for clean delegation
  return session->GetSymbolIndex().GetDocumentSymbols(uri);
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

}  // namespace slangd::services::new_service

#include "slangd/services/legacy/legacy_language_service.hpp"

#include <string>
#include <utility>
#include <vector>

#include <slang/ast/Compilation.h>
#include <slang/diagnostics/DiagnosticEngine.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/text/SourceLocation.h>
#include <slang/text/SourceManager.h>

#include "slangd/services/legacy/definition_provider.hpp"
#include "slangd/services/legacy/diagnostics_provider.hpp"
#include "slangd/services/legacy/symbols_provider.hpp"

namespace slangd {

LegacyLanguageService::LegacyLanguageService(
    asio::any_io_executor executor, std::shared_ptr<spdlog::logger> logger)
    : logger_(logger ? logger : spdlog::default_logger()),
      executor_(std::move(executor)) {
}

auto LegacyLanguageService::InitializeWorkspace(std::string workspace_uri)
    -> asio::awaitable<void> {
  // Create managers with workspace information (same logic as old OnInitialize)
  auto workspace_path = CanonicalPath::FromUri(workspace_uri);

  config_manager_ = ConfigManager::Create(executor_, workspace_path, logger_);
  co_await config_manager_->LoadConfig(workspace_path);

  document_manager_ =
      std::make_shared<DocumentManager>(executor_, config_manager_, logger_);
  workspace_manager_ = std::make_shared<WorkspaceManager>(
      executor_, workspace_path, config_manager_, logger_);

  logger_->debug(
      "LegacyLanguageService initialized for workspace: {}", workspace_uri);
}

auto LegacyLanguageService::ComputeDiagnostics(
    std::string uri, std::string content)
    -> asio::awaitable<std::vector<lsp::Diagnostic>> {
  // Check if workspace is initialized
  if (!document_manager_) {
    logger_->error("LegacyLanguageService: Workspace not initialized");
    co_return std::vector<lsp::Diagnostic>{};
  }

  // Use DiagnosticsProvider logic directly - no provider object needed
  co_await document_manager_->ParseWithCompilation(uri, content);

  // Get diagnostics using DiagnosticsProvider static logic
  auto compilation = document_manager_->GetCompilation(uri);
  auto syntax_tree = document_manager_->GetSyntaxTree(uri);
  auto source_manager = document_manager_->GetSourceManager(uri);

  // If any required component is missing, return empty vector
  if (!compilation || !syntax_tree || !source_manager) {
    co_return std::vector<lsp::Diagnostic>{};
  }

  auto diagnostics = DiagnosticsProvider::ResolveDiagnosticsFromCompilation(
      compilation, syntax_tree, source_manager, uri);

  // Apply filtering and modification to diagnostics - create temporary provider
  // for this
  DiagnosticsProvider temp_provider(
      document_manager_, workspace_manager_, logger_);
  auto filtered_diagnostics =
      temp_provider.FilterAndModifyDiagnostics(std::move(diagnostics));

  logger_->debug(
      "LegacyLanguageService computed {} diagnostics for {}",
      filtered_diagnostics.size(), uri);

  co_return filtered_diagnostics;
}

auto LegacyLanguageService::GetDefinitionsForPosition(
    std::string uri, lsp::Position position) -> std::vector<lsp::Location> {
  // Check if workspace is initialized
  if (!document_manager_) {
    logger_->error("LegacyLanguageService: Workspace not initialized");
    return std::vector<lsp::Location>{};
  }

  // Use DefinitionProvider logic directly
  DefinitionProvider temp_provider(
      document_manager_, workspace_manager_, logger_);
  return temp_provider.GetDefinitionForUri(uri, position);
}

auto LegacyLanguageService::GetDocumentSymbols(std::string uri)
    -> std::vector<lsp::DocumentSymbol> {
  // Check if workspace is initialized
  if (!document_manager_) {
    logger_->error("LegacyLanguageService: Workspace not initialized");
    return std::vector<lsp::DocumentSymbol>{};
  }

  // Use SymbolsProvider logic directly
  SymbolsProvider temp_provider(document_manager_, workspace_manager_, logger_);
  return temp_provider.GetSymbolsForUri(uri);
}

auto LegacyLanguageService::HandleConfigChange() -> void {
  config_manager_->RebuildLayout();
}

}  // namespace slangd

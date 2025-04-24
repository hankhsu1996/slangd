#include "slangd/features/definition_provider.hpp"

#include <slang/ast/ASTVisitor.h>
#include <slang/syntax/AllSyntax.h>
#include <slang/syntax/SyntaxVisitor.h>
#include <spdlog/spdlog.h>

#include "slangd/utils/conversion.hpp"
#include "slangd/utils/path_utils.hpp"

namespace slangd {

auto DefinitionProvider::GetDefinitionForUri(
    std::string uri, lsp::Position position) -> std::vector<lsp::Location> {
  logger_->debug(
      "DefinitionProvider getting definition for location: {}:{}:{}", uri,
      position.line, position.character);
  auto compilation = document_manager_->GetCompilation(uri);
  auto syntax_tree = document_manager_->GetSyntaxTree(uri);
  auto source_manager = document_manager_->GetSourceManager(uri);
  auto symbol_index = document_manager_->GetSymbolIndex(uri);

  if (!compilation || !syntax_tree || !source_manager) {
    logger_->error("Failed to get compilation, syntax tree, or source manager");
    return std::vector<lsp::Location>{};
  }

  // Get the first buffer
  auto buffers = source_manager->getAllBuffers();
  if (buffers.empty()) {
    logger_->error("DefinitionProvider cannot find buffers for URI: {}", uri);
    return std::vector<lsp::Location>{};
  }
  auto buffer = buffers[0];

  // Convert LSP position to Slang source location using our utility
  auto location =
      ConvertLspPositionToSlangLocation(position, buffer, source_manager);

  // First try using document-specific symbol index
  if (symbol_index) {
    auto locations = ResolveDefinitionFromSymbolIndex(
        *symbol_index, source_manager, location);
    if (!locations.empty()) {
      logger_->debug(
          "DefinitionProvider found definition: {}:{}:{}", locations[0].uri,
          locations[0].range.start.line, locations[0].range.start.character);
      return locations;
    }
  }

  // If not found in document index, try workspace symbol index
  logger_->debug(
      "DefinitionProvider cannot find definition in document index, trying "
      "workspace index");
  auto workspace_locations = GetDefinitionFromWorkspace(uri, position);

  if (!workspace_locations.empty()) {
    logger_->debug(
        "DefinitionProvider found definition in workspace symbol index");
    return workspace_locations;
  }

  // No definition found in either index
  logger_->debug(
      "DefinitionProvider cannot find definition for position {}:{}:{}", uri,
      position.line, position.character);
  return {};
}

auto DefinitionProvider::GetDefinitionFromWorkspace(
    std::string uri, lsp::Position position) -> std::vector<lsp::Location> {
  auto workspace_symbol_index = workspace_manager_->GetSymbolIndex();
  auto source_manager = workspace_manager_->GetSourceManager();
  auto path = CanonicalPath::FromUri(uri);
  auto buffer_id = workspace_manager_->GetBufferIdFromPath(path);
  slang::SourceLocation location =
      ConvertLspPositionToSlangLocation(position, buffer_id, source_manager);

  if (!workspace_symbol_index) {
    logger_->error("DefinitionProvider cannot get workspace symbol index");
    return {};
  }

  if (!source_manager) {
    logger_->error("DefinitionProvider cannot get source manager");
    return {};
  }

  logger_->debug(
      "DefinitionProvider looking up definition in workspace symbol index");

  return ResolveDefinitionFromSymbolIndex(
      *workspace_symbol_index, source_manager, location);
}

auto DefinitionProvider::ResolveDefinitionFromSymbolIndex(
    const semantic::SymbolIndex& index,
    const std::shared_ptr<slang::SourceManager>& source_manager,
    slang::SourceLocation lookup_location) -> std::vector<lsp::Location> {
  // Look up the definition using the symbol index
  auto symbol_key = index.LookupSymbolAt(lookup_location);
  if (!symbol_key) {
    // No symbol found at the given location
    return {};
  }

  // Get the definition location as a range
  auto def_range_opt = index.GetDefinitionRange(*symbol_key);
  if (!def_range_opt) {
    // Definition location not found for symbol
    return {};
  }

  // Access the definition range
  auto& def_range = *def_range_opt;

  // Create a location with a proper range for the symbol
  lsp::Location result_location;

  result_location.uri =
      PathToUri(source_manager->getFullPath(def_range.start().buffer()));

  // Convert start position
  auto start_line = source_manager->getLineNumber(def_range.start());
  auto start_column = source_manager->getColumnNumber(def_range.start());
  result_location.range.start = lsp::Position{
      .line = static_cast<int>(start_line - 1),
      .character = static_cast<int>(start_column - 1)};

  // Convert end position
  auto end_line = source_manager->getLineNumber(def_range.end());
  auto end_column = source_manager->getColumnNumber(def_range.end());
  result_location.range.end = lsp::Position{
      .line = static_cast<int>(end_line - 1),
      .character = static_cast<int>(end_column - 1)};

  return {result_location};
}

}  // namespace slangd

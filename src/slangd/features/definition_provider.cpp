#include "slangd/features/definition_provider.hpp"

#include <slang/ast/ASTVisitor.h>
#include <slang/syntax/AllSyntax.h>
#include <slang/syntax/SyntaxVisitor.h>
#include <spdlog/spdlog.h>

#include "slangd/utils/conversion.hpp"

namespace slangd {

auto DefinitionProvider::GetDefinitionForUri(
    std::string uri, lsp::Position position) -> std::vector<lsp::Location> {
  auto compilation = document_manager_->GetCompilation(uri);
  auto syntax_tree = document_manager_->GetSyntaxTree(uri);
  auto source_manager = document_manager_->GetSourceManager(uri);
  auto symbol_index = document_manager_->GetSymbolIndex(uri);

  logger_->info("DefinitionProvider get definition for uri: {}", uri);

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

  spdlog::info("DefinitionProvider location: {}", location.offset());

  // If we have a symbol index, try using it
  if (symbol_index) {
    auto locations = ResolveDefinitionFromSymbolIndex(
        *symbol_index, source_manager, location);
    if (!locations.empty()) {
      return locations;
    }
  }

  // No definition found
  logger_->debug(
      "DefinitionProvider cannot find definition for position {}:{}",
      position.line, position.character);
  return {};
}

auto DefinitionProvider::ResolveDefinitionFromSymbolIndex(
    const semantic::SymbolIndex& index,
    const std::shared_ptr<slang::SourceManager>& source_manager,
    slang::SourceLocation location) -> std::vector<lsp::Location> {
  // Look up the definition using the symbol index
  auto symbol_key = index.LookupSymbolAt(location);
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
      lsp::DocumentUri(source_manager->getFileName(def_range.start()));

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

#include "slangd/document_manager.hpp"

#include <memory>
#include <string>

#include <slang/ast/Compilation.h>
#include <slang/ast/Symbol.h>
#include <slang/ast/symbols/BlockSymbols.h>
#include <slang/ast/symbols/CompilationUnitSymbols.h>
#include <slang/ast/symbols/InstanceSymbols.h>
#include <slang/ast/symbols/MemberSymbols.h>
#include <slang/ast/symbols/VariableSymbols.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/text/SourceManager.h>
#include <spdlog/spdlog.h>

#include "slangd/symbol_utils.hpp"

namespace slangd {

// Forward declaration of recursive helper
void CollectSymbolsRecursively(
    const slang::ast::Symbol& symbol,
    std::vector<std::shared_ptr<const slang::ast::Symbol>>& symbols,
    const std::shared_ptr<slang::ast::Compilation>& compilation);

DocumentManager::DocumentManager(asio::io_context& io_context)
    : io_context_(io_context), strand_(asio::make_strand(io_context)) {
  spdlog::info("DocumentManager initialized");
}

asio::awaitable<std::expected<void, ParseError>> DocumentManager::ParseDocument(
    const std::string& uri, const std::string& content) {
  // Ensure thread safety for data structures
  co_await asio::post(strand_, asio::use_awaitable);

  try {
    // Create a new source manager for this document if it doesn't exist
    if (source_managers_.find(uri) == source_managers_.end()) {
      source_managers_[uri] = std::make_shared<slang::SourceManager>();
    }
    // Use direct reference to the managed SourceManager object
    auto& source_manager = *source_managers_[uri];

    // Parse the document
    std::string_view content_view(content);
    auto syntax_tree =
        slang::syntax::SyntaxTree::fromText(content_view, source_manager, uri);

    // Store the syntax tree
    syntax_trees_[uri] = syntax_tree;

    // Create a new compilation if needed
    if (compilations_.find(uri) == compilations_.end()) {
      compilations_[uri] = std::make_shared<slang::ast::Compilation>();
    }

    // Add the syntax tree to the compilation
    auto& compilation = compilations_[uri];
    compilation->addSyntaxTree(syntax_tree);

    spdlog::info("Successfully parsed {}", uri);
    co_return std::expected<void, ParseError>{};
  } catch (const std::exception& e) {
    spdlog::error("Error parsing document {}: {}", uri, e.what());
    co_return std::unexpected(ParseError::SlangInternalError);
  } catch (...) {
    spdlog::error("Unknown error parsing document {}", uri);
    co_return std::unexpected(ParseError::UnknownError);
  }
}

asio::awaitable<std::shared_ptr<slang::syntax::SyntaxTree>>
DocumentManager::GetSyntaxTree(const std::string& uri) {
  // Ensure thread safety
  co_await asio::post(strand_, asio::use_awaitable);

  auto it = syntax_trees_.find(uri);
  if (it != syntax_trees_.end()) {
    co_return it->second;
  }
  co_return nullptr;
}

asio::awaitable<std::shared_ptr<slang::ast::Compilation>>
DocumentManager::GetCompilation(const std::string& uri) {
  // Ensure thread safety
  co_await asio::post(strand_, asio::use_awaitable);

  auto it = compilations_.find(uri);
  if (it != compilations_.end()) {
    co_return it->second;
  }
  co_return nullptr;
}

asio::awaitable<std::vector<std::shared_ptr<const slang::ast::Symbol>>>
DocumentManager::GetSymbols(const std::string& uri) {
  // Ensure thread safety
  co_await asio::post(strand_, asio::use_awaitable);

  std::vector<std::shared_ptr<const slang::ast::Symbol>> symbols;

  // Get the compilation for this document
  auto it = compilations_.find(uri);
  if (it == compilations_.end()) {
    co_return symbols;  // Empty vector if no compilation exists
  }

  auto& compilation = it->second;

  // Create a shared_ptr for each compilation definition
  // This uses a custom deleter that doesn't actually delete the symbol
  // since the symbol is owned by the compilation
  for (const auto* definition : compilation->getDefinitions()) {
    if (definition) {
      auto symbol_ptr = std::shared_ptr<const slang::ast::Symbol>(
          definition, [compilation](const slang::ast::Symbol*) {
            // No deletion - the compilation owns the symbol
          });
      symbols.push_back(symbol_ptr);
    }
  }

  // Also include the root as a symbol
  auto& root = compilation->getRoot();
  auto root_ptr = std::shared_ptr<const slang::ast::Symbol>(
      &root, [compilation](const slang::ast::Symbol*) {
        // No deletion - the compilation owns the symbol
      });
  symbols.push_back(root_ptr);

  // Output the number of symbols found for debugging
  spdlog::info("Found {} symbols in document: {}", symbols.size(), uri);

  co_return symbols;
}

asio::awaitable<std::vector<lsp::DocumentSymbol>>
DocumentManager::GetDocumentSymbols(const std::string& uri) {
  // Ensure thread safety
  co_await asio::post(strand_, asio::use_awaitable);

  std::vector<lsp::DocumentSymbol> document_symbols;

  // Get the compilation for this document
  auto comp_it = compilations_.find(uri);
  auto sm_it = source_managers_.find(uri);

  // If either compilation or source manager is missing, return empty vector
  if (comp_it == compilations_.end() || sm_it == source_managers_.end()) {
    co_return document_symbols;
  }

  auto& compilation = comp_it->second;
  auto& source_manager = sm_it->second;

  // Use the symbol utility to extract document symbols
  document_symbols =
      slangd::GetDocumentSymbols(*compilation, source_manager, uri);

  co_return document_symbols;
}

}  // namespace slangd

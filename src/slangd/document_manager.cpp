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
#include <slang/diagnostics/DiagnosticEngine.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/text/SourceManager.h>
#include <spdlog/spdlog.h>

#include "slangd/features/diagnostics.hpp"
#include "slangd/features/symbols.hpp"

namespace slangd {

// Forward declaration of recursive helper
void CollectSymbolsRecursively(
    const slang::ast::Symbol& symbol,
    std::vector<std::shared_ptr<const slang::ast::Symbol>>& symbols,
    const std::shared_ptr<slang::ast::Compilation>& compilation);

DocumentManager::DocumentManager(asio::any_io_executor executor)
    : executor_(executor), strand_(asio::make_strand(executor)) {
}

auto DocumentManager::ParseWithCompilation(std::string uri, std::string content)
    -> asio::awaitable<std::expected<void, ParseError>> {
  // Ensure thread safety for data structures
  co_await asio::post(strand_, asio::use_awaitable);

  // Create a new source manager for this document if it doesn't exist
  if (source_managers_.find(uri) == source_managers_.end()) {
    source_managers_[uri] = std::make_shared<slang::SourceManager>();
  }

  auto& source_manager = *source_managers_[uri];
  std::string_view content_view(content);

  // Parse the document to create a syntax tree
  auto syntax_tree =
      slang::syntax::SyntaxTree::fromText(content_view, source_manager, uri);

  // This should be extremely rare - handle only critical failures
  if (!syntax_tree) {
    spdlog::error("Critical failure creating syntax tree for document {}", uri);
    co_return std::unexpected(ParseError::kSlangInternalError);
  }

  // Store the syntax tree
  syntax_trees_[uri] = syntax_tree;

  // Create compilation options with lint mode enabled
  slang::ast::CompilationOptions options;
  options.flags |= slang::ast::CompilationFlags::LintMode;

  // Create a new compilation with lint mode options
  compilations_[uri] = std::make_shared<slang::ast::Compilation>(options);
  auto& compilation = *compilations_[uri];

  // Add the syntax tree to the compilation
  compilation.addSyntaxTree(syntax_trees_[uri]);

  spdlog::debug("DocumentManager compilation completed for document: {}", uri);
  co_return std::expected<void, ParseError>{};
}

auto DocumentManager::ParseWithElaboration(std::string uri, std::string content)
    -> asio::awaitable<std::expected<void, ParseError>> {
  // First perform basic compilation
  auto compilation_result = co_await ParseWithCompilation(uri, content);
  if (!compilation_result) {
    co_return compilation_result;  // Forward the error
  }

  // Ensure thread safety for elaboration
  co_await asio::post(strand_, asio::use_awaitable);

  // Ensure we have a compilation
  auto comp_it = compilations_.find(uri);
  if (comp_it == compilations_.end()) {
    spdlog::error("No compilation found for document: {}", uri);
    co_return std::unexpected(ParseError::kCompilationError);
  }

  // Get the root to force elaboration
  auto& compilation = *comp_it->second;

  // Handle elaboration failures via diagnostics, not exceptions
  compilation.getRoot();

  spdlog::debug(
      "DocumentManager full elaboration completed for document: {}", uri);
  co_return std::expected<void, ParseError>{};
}

auto DocumentManager::GetSyntaxTree(std::string uri)
    -> asio::awaitable<std::shared_ptr<slang::syntax::SyntaxTree>> {
  // Ensure thread safety
  co_await asio::post(strand_, asio::use_awaitable);

  auto it = syntax_trees_.find(uri);
  if (it != syntax_trees_.end()) {
    co_return it->second;
  }
  co_return nullptr;
}

auto DocumentManager::GetCompilation(std::string uri)
    -> asio::awaitable<std::shared_ptr<slang::ast::Compilation>> {
  // Ensure thread safety
  co_await asio::post(strand_, asio::use_awaitable);

  auto it = compilations_.find(uri);
  if (it != compilations_.end()) {
    co_return it->second;
  }
  co_return nullptr;
}

auto DocumentManager::GetSymbols(std::string uri)
    -> asio::awaitable<std::vector<std::shared_ptr<const slang::ast::Symbol>>> {
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
    if (definition != nullptr) {
      auto symbol_ptr = std::shared_ptr<const slang::ast::Symbol>(
          definition, [compilation](const slang::ast::Symbol*) {
            // No deletion - the compilation owns the symbol
          });
      symbols.push_back(symbol_ptr);
    }
  }

  // Also include the root as a symbol
  const auto& root = compilation->getRoot();
  auto root_ptr = std::shared_ptr<const slang::ast::Symbol>(
      &root, [compilation](const slang::ast::Symbol*) {
        // No deletion - the compilation owns the symbol
      });
  symbols.push_back(root_ptr);

  // Output the number of symbols found for debugging
  spdlog::debug(
      "DocumentManager found {} symbols in document: {}", symbols.size(), uri);

  co_return symbols;
}

auto DocumentManager::GetDocumentSymbols(std::string uri)
    -> asio::awaitable<std::vector<lsp::DocumentSymbol>> {
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

auto DocumentManager::GetDocumentDiagnostics(std::string uri)
    -> asio::awaitable<std::vector<lsp::Diagnostic>> {
  // Ensure thread safety
  co_await asio::post(strand_, asio::use_awaitable);

  std::vector<lsp::Diagnostic> diagnostics;

  // Get the compilation and syntax tree for this document
  auto comp_it = compilations_.find(uri);
  auto tree_it = syntax_trees_.find(uri);
  auto sm_it = source_managers_.find(uri);

  // If any required component is missing, return empty vector
  if (comp_it == compilations_.end() || tree_it == syntax_trees_.end() ||
      sm_it == source_managers_.end()) {
    co_return diagnostics;
  }

  auto& compilation = comp_it->second;
  auto& syntax_tree = tree_it->second;
  auto& source_manager = sm_it->second;

  // Create a diagnostic engine using the document's source manager
  // This ensures proper location information for diagnostics
  slang::DiagnosticEngine diagnostic_engine(*source_manager);

  // Use the diagnostic utility to extract diagnostics
  diagnostics = slangd::GetDocumentDiagnostics(
      syntax_tree, compilation, source_manager, diagnostic_engine, uri);

  spdlog::debug(
      "DocumentManager found {} diagnostics in document: {}",
      diagnostics.size(), uri);

  co_return diagnostics;
}

}  // namespace slangd

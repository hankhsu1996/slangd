#include "slangd/core/document_manager.hpp"

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
#include <slang/parsing/Preprocessor.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/text/SourceManager.h>
#include <slang/util/Bag.h>
#include <spdlog/spdlog.h>

namespace slangd {

DocumentManager::DocumentManager(
    asio::any_io_executor executor,
    std::shared_ptr<ConfigManager> config_manager,
    std::shared_ptr<spdlog::logger> logger)
    : executor_(executor),
      logger_(logger ? logger : spdlog::default_logger()),
      config_manager_(config_manager) {
}

auto DocumentManager::ParseWithCompilation(std::string uri, std::string content)
    -> asio::awaitable<void> {
  source_managers_[uri] = std::make_shared<slang::SourceManager>();
  auto& source_manager = *source_managers_[uri];
  std::string_view content_view(content);

  // Parse the document to create a syntax tree
  auto syntax_tree =
      slang::syntax::SyntaxTree::fromText(content_view, source_manager, uri);

  // This should be extremely rare - handle only critical failures
  if (!syntax_tree) {
    Logger()->error(
        "Critical failure creating syntax tree for document {}", uri);
    co_return;
  }

  // Store the syntax tree
  syntax_trees_[uri] = syntax_tree;

  // Create compilation options with lint mode enabled
  slang::ast::CompilationOptions comp_options;
  comp_options.flags |= slang::ast::CompilationFlags::LintMode;

  // Create preprocessor options for defines
  slang::Bag options;
  options.set(comp_options);

  // Apply configuration settings from ConfigManager
  if (config_manager_) {
    // Get preprocessing defines from config
    slang::parsing::PreprocessorOptions pp_options;

    // Add defines from configuration
    for (const auto& define : config_manager_->GetDefines()) {
      pp_options.predefines.push_back(define);
    }

    // Add preprocessor options to the bag
    options.set(pp_options);

    // Log include directories (we don't directly apply them to the source
    // manager)
    auto include_dirs = config_manager_->GetIncludeDirectories();
    if (!include_dirs.empty()) {
      Logger()->debug(
          "DocumentManager has {} include directories available (applied "
          "during imports)",
          include_dirs.size());
    }

    Logger()->debug(
        "DocumentManager applied config settings to compilation for: {}", uri);
  }

  // Create a new compilation with the options bag
  compilations_[uri] = std::make_shared<slang::ast::Compilation>(options);
  auto& compilation = *compilations_[uri];

  // Add the syntax tree to the compilation
  compilation.addSyntaxTree(syntax_trees_[uri]);

  // Build a basic symbol index (definitions only) for quick navigation
  symbol_indices_[uri] = std::make_shared<semantic::SymbolIndex>(
      semantic::SymbolIndex::FromCompilation(compilation));

  Logger()->debug(
      "DocumentManager compilation completed for document: {}", uri);
  co_return;
}

auto DocumentManager::ParseWithElaboration(std::string uri, std::string content)
    -> asio::awaitable<void> {
  // First perform basic compilation
  co_await ParseWithCompilation(uri, content);

  // Ensure we have a compilation
  auto comp_it = compilations_.find(uri);
  if (comp_it == compilations_.end()) {
    Logger()->error("No compilation found for document: {}", uri);
    co_return;
  }

  // Get the root to force elaboration
  auto& compilation = *comp_it->second;

  // Handle elaboration failures via diagnostics, not exceptions
  compilation.getRoot();

  // Build a comprehensive symbol index with references after full elaboration
  symbol_indices_[uri] = std::make_shared<semantic::SymbolIndex>(
      semantic::SymbolIndex::FromCompilation(compilation));

  Logger()->debug(
      "DocumentManager full elaboration completed for document: {}", uri);
  co_return;
}

auto DocumentManager::GetSyntaxTree(std::string uri)
    -> std::shared_ptr<slang::syntax::SyntaxTree> {
  auto it = syntax_trees_.find(uri);
  if (it != syntax_trees_.end()) {
    return it->second;
  }
  return nullptr;
}

auto DocumentManager::GetCompilation(std::string uri)
    -> std::shared_ptr<slang::ast::Compilation> {
  auto it = compilations_.find(uri);
  if (it != compilations_.end()) {
    return it->second;
  }
  return nullptr;
}

auto DocumentManager::GetSourceManager(std::string uri)
    -> std::shared_ptr<slang::SourceManager> {
  auto it = source_managers_.find(uri);
  if (it != source_managers_.end()) {
    return it->second;
  }
  return nullptr;
}

auto DocumentManager::GetSymbolIndex(std::string uri)
    -> std::shared_ptr<semantic::SymbolIndex> {
  auto it = symbol_indices_.find(uri);
  if (it != symbol_indices_.end()) {
    return it->second;
  }
  return nullptr;
}

auto DocumentManager::GetSymbols(std::string uri)
    -> std::vector<std::shared_ptr<const slang::ast::Symbol>> {
  std::vector<std::shared_ptr<const slang::ast::Symbol>> symbols;

  // Get the compilation for this document
  auto it = compilations_.find(uri);
  if (it == compilations_.end()) {
    return symbols;  // Empty vector if no compilation exists
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
  Logger()->debug(
      "DocumentManager found {} symbols in document: {}", symbols.size(), uri);

  return symbols;
}

}  // namespace slangd

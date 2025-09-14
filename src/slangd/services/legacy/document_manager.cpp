#include "slangd/services/legacy/document_manager.hpp"

#include <memory>
#include <string>
#include <utility>

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

namespace slangd {

DocumentManager::DocumentManager(
    asio::any_io_executor executor,
    std::shared_ptr<ProjectLayoutService> config_manager,
    std::shared_ptr<spdlog::logger> logger)
    : executor_(std::move(executor)),
      logger_(logger ? logger : spdlog::default_logger()),
      layout_service_(std::move(config_manager)) {
}

auto DocumentManager::ParseWithCompilation(std::string uri, std::string content)
    -> asio::awaitable<void> {
  auto path = CanonicalPath::FromUri(uri);
  source_managers_[path] = std::make_shared<slang::SourceManager>();
  auto& source_manager = *source_managers_[path];

  // Prepare preprocessor options
  slang::Bag options;
  slang::parsing::PreprocessorOptions pp_options;
  for (const auto& define : layout_service_->GetDefines()) {
    pp_options.predefines.push_back(define);
  }
  for (const auto& include_dir : layout_service_->GetIncludeDirectories()) {
    pp_options.additionalIncludePaths.emplace_back(include_dir.Path());
  }
  options.set(pp_options);

  // Parse the document to create a syntax tree
  auto buffer = source_manager.assignText(path.String(), content);
  auto syntax_tree =
      slang::syntax::SyntaxTree::fromBuffer(buffer, source_manager, options);

  // This should be extremely rare - handle only critical failures
  if (!syntax_tree) {
    logger_->error(
        "Critical failure creating syntax tree for document {}", uri);
    co_return;
  }

  // Store the syntax tree
  syntax_trees_[path] = syntax_tree;

  // Create compilation options with lint mode and language server mode enabled
  slang::ast::CompilationOptions comp_options;
  comp_options.flags |= slang::ast::CompilationFlags::LintMode;
  comp_options.flags |= slang::ast::CompilationFlags::LanguageServerMode;
  options.set(comp_options);

  // Create a new compilation with the options bag
  compilations_[path] = std::make_shared<slang::ast::Compilation>(options);
  auto& compilation = *compilations_[path];

  // Add the syntax tree to the compilation
  compilation.addSyntaxTree(syntax_trees_[path]);

  // Build a basic symbol index (definitions only) for quick navigation
  symbol_indices_[path] = std::make_shared<semantic::SymbolIndex>(
      semantic::SymbolIndex::FromCompilation(
          compilation, {buffer.id}, logger_));

  logger_->debug("DocumentManager compilation completed for document: {}", uri);
  co_return;
}

auto DocumentManager::ParseWithElaboration(std::string uri, std::string content)
    -> asio::awaitable<void> {
  auto path = CanonicalPath::FromUri(uri);

  // First perform basic compilation
  co_await ParseWithCompilation(uri, content);

  // Ensure we have a compilation
  auto comp_it = compilations_.find(path);
  if (comp_it == compilations_.end()) {
    logger_->error("No compilation found for document: {}", uri);
    co_return;
  }

  // Get the root to force elaboration
  auto& compilation = *comp_it->second;

  // Handle elaboration failures via diagnostics, not exceptions
  compilation.getRoot();

  logger_->debug(
      "DocumentManager full elaboration completed for document: {}", uri);
  co_return;
}

auto DocumentManager::GetSyntaxTree(std::string uri)
    -> std::shared_ptr<slang::syntax::SyntaxTree> {
  auto path = CanonicalPath::FromUri(uri);
  auto it = syntax_trees_.find(path);
  if (it != syntax_trees_.end()) {
    return it->second;
  }
  return nullptr;
}

auto DocumentManager::GetCompilation(std::string uri)
    -> std::shared_ptr<slang::ast::Compilation> {
  auto path = CanonicalPath::FromUri(uri);
  auto it = compilations_.find(path);
  if (it != compilations_.end()) {
    return it->second;
  }
  return nullptr;
}

auto DocumentManager::GetSourceManager(std::string uri)
    -> std::shared_ptr<slang::SourceManager> {
  auto path = CanonicalPath::FromUri(uri);
  auto it = source_managers_.find(path);
  if (it != source_managers_.end()) {
    return it->second;
  }
  return nullptr;
}

auto DocumentManager::GetSymbolIndex(std::string uri)
    -> std::shared_ptr<semantic::SymbolIndex> {
  auto path = CanonicalPath::FromUri(uri);
  auto it = symbol_indices_.find(path);
  if (it != symbol_indices_.end()) {
    return it->second;
  }
  return nullptr;
}

}  // namespace slangd

#include "slangd/services/preamble_manager.hpp"

#include <slang/ast/ASTVisitor.h>
#include <slang/ast/Compilation.h>
#include <slang/ast/SemanticFacts.h>
#include <slang/ast/symbols/CompilationUnitSymbols.h>
#include <slang/parsing/Preprocessor.h>
#include <slang/syntax/AllSyntax.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/util/Bag.h>

#include "slangd/core/project_layout_service.hpp"
#include "slangd/utils/compilation_options.hpp"
#include "slangd/utils/scoped_timer.hpp"

namespace slangd::services {

auto PreambleManager::CreateFromProjectLayout(
    std::shared_ptr<ProjectLayoutService> layout_service,
    std::shared_ptr<spdlog::logger> logger)
    -> std::shared_ptr<PreambleManager> {
  if (!layout_service) {
    if (logger) {
      logger->error("PreambleManager: ProjectLayoutService is null");
    }
    return nullptr;
  }

  logger->debug("PreambleManager: Creating from ProjectLayoutService");

  // Create preamble_manager instance and initialize it
  auto preamble_manager = std::make_shared<PreambleManager>();
  preamble_manager->BuildFromLayout(layout_service, logger);

  logger->debug("PreambleManager: Created");

  return preamble_manager;
}

auto PreambleManager::BuildFromLayout(
    std::shared_ptr<ProjectLayoutService> layout_service,
    std::shared_ptr<spdlog::logger> logger) -> void {
  logger_ = logger;
  utils::ScopedTimer timer("PreambleManager build", logger_);
  logger_->debug("PreambleManager: Building from layout service");

  // Create fresh source manager for preamble compilation
  source_manager_ = std::make_shared<slang::SourceManager>();

  // Start with standard LSP compilation options
  auto options = utils::CreateLspCompilationOptions();

  // Get include directories and defines from layout service
  include_directories_ = layout_service->GetIncludeDirectories();
  defines_ = layout_service->GetDefines();

  // Add project-specific preprocessor options
  auto pp_options = options.getOrDefault<slang::parsing::PreprocessorOptions>();
  for (const auto& include_dir : include_directories_) {
    pp_options.additionalIncludePaths.emplace_back(include_dir.Path());
  }
  for (const auto& define : defines_) {
    pp_options.predefines.push_back(define);
  }
  options.set(pp_options);

  // Create preamble compilation with options
  preamble_compilation_ = std::make_shared<slang::ast::Compilation>(options);

  logger_->debug(
      "PreambleManager: Applied {} include dirs, {} defines",
      include_directories_.size(), defines_.size());

  // Get all source files from layout service
  auto source_files = layout_service->GetSourceFiles();
  logger_->debug(
      "PreambleManager: Processing {} source files", source_files.size());

  // Add all source files to preamble compilation
  for (const auto& file_path : source_files) {
    auto tree_result = slang::syntax::SyntaxTree::fromFile(
        file_path.Path().string(), *source_manager_, options);

    if (tree_result) {
      preamble_compilation_->addSyntaxTree(tree_result.value());
    } else {
      logger_->warn(
          "PreambleManager: Failed to parse file: {}",
          file_path.Path().string());
    }
  }

  auto elapsed = timer.GetElapsed();
  logger_->info(
      "PreambleManager: Build complete ({})",
      utils::ScopedTimer::FormatDuration(elapsed));
}

auto PreambleManager::GetPackageMap() const -> const
    slang::flat_hash_map<std::string_view, const slang::ast::PackageSymbol*>& {
  return preamble_compilation_->getPackageMap();
}

auto PreambleManager::GetDefinitionMap() const -> const slang::flat_hash_map<
    std::tuple<std::string_view, const slang::ast::Scope*>,
    std::pair<std::vector<const slang::ast::Symbol*>, bool>>& {
  return preamble_compilation_->getDefinitionMap();
}

auto PreambleManager::GetIncludeDirectories() const
    -> const std::vector<CanonicalPath>& {
  return include_directories_;
}

auto PreambleManager::GetDefines() const -> const std::vector<std::string>& {
  return defines_;
}

auto PreambleManager::GetSourceManager() const -> const slang::SourceManager& {
  return *source_manager_;
}

auto PreambleManager::GetCompilation() const -> const slang::ast::Compilation& {
  return *preamble_compilation_;
}

}  // namespace slangd::services

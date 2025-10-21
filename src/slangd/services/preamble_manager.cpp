#include "slangd/services/preamble_manager.hpp"

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/post.hpp>
#include <asio/use_awaitable.hpp>
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
    asio::any_io_executor compilation_executor,
    std::shared_ptr<spdlog::logger> logger)
    -> asio::awaitable<std::shared_ptr<PreambleManager>> {
  if (!layout_service) {
    if (logger) {
      logger->error("PreambleManager: ProjectLayoutService is null");
    }
    co_return nullptr;
  }

  logger->debug("PreambleManager: Creating from ProjectLayoutService");

  // Create preamble_manager instance and initialize it
  auto preamble_manager = std::make_shared<PreambleManager>();
  co_await preamble_manager->BuildFromLayout(
      layout_service, logger, compilation_executor);

  logger->debug("PreambleManager: Created");

  co_return preamble_manager;
}

auto PreambleManager::BuildFromLayout(
    std::shared_ptr<ProjectLayoutService> layout_service,
    std::shared_ptr<spdlog::logger> logger,
    asio::any_io_executor compilation_executor) -> asio::awaitable<void> {
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

  if (source_files.empty()) {
    logger_->warn("PreambleManager: No source files to process");
    co_return;
  }

  // Pre-allocate results vector
  using TreeResult = std::optional<std::shared_ptr<slang::syntax::SyntaxTree>>;
  std::vector<TreeResult> results(source_files.size());

  // Spawn parallel parsing tasks on compilation pool
  std::vector<asio::awaitable<void>> parse_tasks;
  parse_tasks.reserve(source_files.size());

  for (size_t i = 0; i < source_files.size(); ++i) {
    parse_tasks.push_back(
        asio::co_spawn(
            compilation_executor,
            [this, i, &source_files, &results,
             &options]() -> asio::awaitable<void> {
              auto tree_result = slang::syntax::SyntaxTree::fromFile(
                  source_files[i].Path().string(), *source_manager_, options);

              if (tree_result) {
                results[i] = tree_result.value();
              } else {
                results[i] = std::nullopt;
              }
              co_return;
            },
            asio::use_awaitable));
  }

  // Wait for all parallel parsing to complete (non-blocking async wait)
  for (auto& task : parse_tasks) {
    co_await std::move(task);
  }

  // Add trees to compilation sequentially (addSyntaxTree is NOT thread-safe)
  for (size_t i = 0; i < results.size(); ++i) {
    if (results[i]) {
      preamble_compilation_->addSyntaxTree(*results[i]);
    } else {
      logger_->warn(
          "PreambleManager: Failed to parse file: {}",
          source_files[i].Path().string());
    }
  }

  auto elapsed = timer.GetElapsed();
  logger_->info(
      "PreambleManager: Build complete ({}, {} files, parallel parsing)",
      utils::ScopedTimer::FormatDuration(elapsed), source_files.size());

  co_return;
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

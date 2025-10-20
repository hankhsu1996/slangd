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

  if (!logger) {
    logger = spdlog::default_logger();
  }

  logger->debug("PreambleManager: Creating from ProjectLayoutService");

  // Create preamble_manager instance and initialize it
  auto preamble_manager = std::make_shared<PreambleManager>();
  preamble_manager->BuildFromLayout(layout_service, logger);

  logger->debug(
      "PreambleManager: Created with {} packages, version {}",
      preamble_manager->packages_.size(), preamble_manager->version_);

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

  auto packages = preamble_compilation_->getPackages();
  logger_->debug("PreambleManager: Extracting {} packages", packages.size());

  packages_.clear();

  for (const auto* package : packages) {
    if (package == nullptr) {
      continue;
    }

    auto file_path_str = source_manager_->getFileName(package->location);
    CanonicalPath package_file_path(file_path_str);

    packages_[std::string(package->name)] = PackageEntry{
        .symbol = package, .file_path = std::move(package_file_path)};
  }

  logger_->debug("PreambleManager: Extracted {} packages", packages_.size());

  auto definitions = preamble_compilation_->getDefinitions();
  logger_->debug(
      "PreambleManager: Extracting interfaces from {} definitions",
      definitions.size());

  interfaces_.clear();

  for (const auto* symbol : definitions) {
    if (symbol == nullptr) {
      continue;
    }

    if (symbol->kind == slang::ast::SymbolKind::Definition) {
      const auto& definition = symbol->as<slang::ast::DefinitionSymbol>();

      if (definition.definitionKind == slang::ast::DefinitionKind::Interface) {
        auto file_path_str = source_manager_->getFileName(definition.location);
        CanonicalPath interface_file_path(file_path_str);

        interfaces_[std::string(definition.name)] = InterfaceEntry{
            .definition = &definition,
            .file_path = std::move(interface_file_path)};
      }
    }
  }

  logger_->debug(
      "PreambleManager: Extracted {} interfaces", interfaces_.size());

  logger_->debug("PreambleManager: Extracting modules from definitions");

  modules_.clear();

  for (const auto* symbol : definitions) {
    if (symbol == nullptr) {
      continue;
    }

    if (symbol->kind == slang::ast::SymbolKind::Definition) {
      const auto& definition = symbol->as<slang::ast::DefinitionSymbol>();

      if (definition.definitionKind == slang::ast::DefinitionKind::Module) {
        auto file_path_str = source_manager_->getFileName(definition.location);
        CanonicalPath module_file_path(file_path_str);

        modules_[std::string(definition.name)] = ModuleEntry{
            .definition = &definition,
            .file_path = std::move(module_file_path)};
      }
    }
  }

  auto elapsed = timer.GetElapsed();
  logger_->info(
      "PreambleManager: Build complete - {} packages, {} interfaces, {} "
      "modules "
      "({})",
      packages_.size(), interfaces_.size(), modules_.size(),
      utils::ScopedTimer::FormatDuration(elapsed));
}

auto PreambleManager::GetPackages() const
    -> const std::unordered_map<std::string, PackageEntry>& {
  return packages_;
}

auto PreambleManager::GetInterfaces() const
    -> const std::unordered_map<std::string, InterfaceEntry>& {
  return interfaces_;
}

auto PreambleManager::GetModules() const
    -> const std::unordered_map<std::string, ModuleEntry>& {
  return modules_;
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

auto PreambleManager::GetVersion() const -> uint64_t {
  return version_;
}

auto PreambleManager::GetPackage(std::string_view name) const
    -> const slang::ast::PackageSymbol* {
  auto it = packages_.find(std::string(name));
  if (it != packages_.end()) {
    return it->second.symbol;
  }
  return nullptr;
}

auto PreambleManager::GetInterfaceDefinition(std::string_view name) const
    -> const slang::ast::Symbol* {
  auto it = interfaces_.find(std::string(name));
  if (it != interfaces_.end()) {
    return it->second.definition;
  }
  return nullptr;
}

auto PreambleManager::GetModuleDefinition(std::string_view name) const
    -> const slang::ast::Symbol* {
  auto it = modules_.find(std::string(name));
  if (it != modules_.end()) {
    return it->second.definition;
  }
  return nullptr;
}

}  // namespace slangd::services

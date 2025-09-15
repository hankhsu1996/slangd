#include "slangd/core/global_catalog.hpp"

#include <slang/ast/Compilation.h>
#include <slang/ast/symbols/CompilationUnitSymbols.h>
#include <slang/parsing/Preprocessor.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/util/Bag.h>

#include "slangd/core/project_layout_service.hpp"

namespace slangd {

auto GlobalCatalog::CreateFromProjectLayout(
    std::shared_ptr<ProjectLayoutService> layout_service,
    std::shared_ptr<spdlog::logger> logger) -> std::shared_ptr<GlobalCatalog> {
  if (!layout_service) {
    if (logger) {
      logger->error("GlobalCatalog: ProjectLayoutService is null");
    }
    return nullptr;
  }

  if (!logger) {
    logger = spdlog::default_logger();
  }

  logger->debug("GlobalCatalog: Creating from ProjectLayoutService");

  // Create catalog instance and initialize it
  auto catalog = std::make_shared<GlobalCatalog>();
  catalog->BuildFromLayout(layout_service, logger);

  logger->debug(
      "GlobalCatalog: Created with {} packages, version {}",
      catalog->packages_.size(), catalog->version_);

  return catalog;
}

auto GlobalCatalog::BuildFromLayout(
    std::shared_ptr<ProjectLayoutService> layout_service,
    std::shared_ptr<spdlog::logger> logger) -> void {
  logger_ = logger;
  logger_->debug("GlobalCatalog: Building from layout service");

  // Create fresh source manager for global compilation
  source_manager_ = std::make_shared<slang::SourceManager>();

  // Prepare preprocessor options from layout service
  slang::Bag options;
  slang::parsing::PreprocessorOptions pp_options;

  // Get include directories and defines from layout service
  include_directories_ = layout_service->GetIncludeDirectories();
  defines_ = layout_service->GetDefines();

  // Apply include directories to preprocessor options
  for (const auto& include_dir : include_directories_) {
    pp_options.additionalIncludePaths.emplace_back(include_dir.Path());
  }

  // Apply defines to preprocessor options
  for (const auto& define : defines_) {
    pp_options.predefines.push_back(define);
  }

  options.set(pp_options);

  // Create compilation options for LSP mode
  slang::ast::CompilationOptions comp_options;
  comp_options.flags |= slang::ast::CompilationFlags::LintMode;
  comp_options.flags |= slang::ast::CompilationFlags::LanguageServerMode;
  options.set(comp_options);

  // Create global compilation with options
  global_compilation_ = std::make_shared<slang::ast::Compilation>(options);

  logger_->debug(
      "GlobalCatalog: Applied {} include dirs, {} defines",
      include_directories_.size(), defines_.size());

  // Get all source files from layout service
  auto source_files = layout_service->GetSourceFiles();
  logger_->debug(
      "GlobalCatalog: Processing {} source files", source_files.size());

  // Add all source files to global compilation
  for (const auto& file_path : source_files) {
    auto tree_result = slang::syntax::SyntaxTree::fromFile(
        file_path.Path().string(), *source_manager_, options);

    if (tree_result) {
      global_compilation_->addSyntaxTree(tree_result.value());
      logger_->debug(
          "GlobalCatalog: Added file to compilation: {}",
          file_path.Path().string());
    } else {
      logger_->warn(
          "GlobalCatalog: Failed to parse file: {}", file_path.Path().string());
    }
  }

  // Extract package metadata using safe Slang API (NO getRoot() call!)
  auto packages = global_compilation_->getPackages();
  logger_->debug("GlobalCatalog: Extracting {} packages", packages.size());

  packages_.clear();
  packages_.reserve(packages.size());

  for (const auto* package : packages) {
    if (package == nullptr) {
      continue;
    }

    std::string package_name = std::string(package->name);
    auto file_path_str = source_manager_->getFileName(package->location);
    CanonicalPath package_file_path(file_path_str);

    packages_.push_back(
        {.name = std::move(package_name),
         .file_path = std::move(package_file_path)});

    logger_->debug(
        "GlobalCatalog: Found package '{}' in file: {}", package->name,
        file_path_str);
  }

  // For MVP, interfaces remain empty
  interfaces_.clear();

  logger_->debug(
      "GlobalCatalog: Build complete - {} packages, {} interfaces",
      packages_.size(), interfaces_.size());
}

auto GlobalCatalog::GetPackages() const -> const std::vector<PackageInfo>& {
  return packages_;
}

auto GlobalCatalog::GetInterfaces() const -> const std::vector<InterfaceInfo>& {
  return interfaces_;
}

auto GlobalCatalog::GetIncludeDirectories() const
    -> const std::vector<CanonicalPath>& {
  return include_directories_;
}

auto GlobalCatalog::GetDefines() const -> const std::vector<std::string>& {
  return defines_;
}

auto GlobalCatalog::GetVersion() const -> uint64_t {
  return version_;
}

}  // namespace slangd

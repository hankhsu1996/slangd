#include "slangd/services/preamble_manager.hpp"

#include <slang/ast/ASTVisitor.h>
#include <slang/ast/Compilation.h>
#include <slang/ast/SemanticFacts.h>
#include <slang/ast/symbols/CompilationUnitSymbols.h>
#include <slang/parsing/Preprocessor.h>
#include <slang/syntax/AllSyntax.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/util/Bag.h>

#include "lsp/basic.hpp"
#include "slangd/core/project_layout_service.hpp"
#include "slangd/utils/conversion.hpp"
#include "slangd/utils/scoped_timer.hpp"

namespace slangd::services {

namespace {

// Visitor for collecting all package symbols and their LSP locations
class PreambleSymbolVisitor
    : public slang::ast::ASTVisitor<PreambleSymbolVisitor, true, false> {
 public:
  PreambleSymbolVisitor(
      std::unordered_map<const slang::ast::Symbol*, PreambleSymbolInfo>&
          symbol_info,
      const slang::SourceManager& source_manager,
      std::shared_ptr<spdlog::logger> logger)
      : symbol_info_(symbol_info),
        source_manager_(source_manager),
        logger_(logger ? logger : spdlog::default_logger()) {
  }

  void ProcessSymbol(const slang::ast::Symbol& symbol) {
    // Create LSP range for symbol name
    auto definition_range = CreateSymbolLspRange(symbol, source_manager_.get());
    if (!definition_range) {
      return;  // Skip symbols without valid location
    }

    // Convert symbol location to file URI
    auto file_name = source_manager_.get().getFileName(symbol.location);
    auto canonical_path = CanonicalPath(std::filesystem::path(file_name));
    auto file_uri = canonical_path.ToUri();

    // Store in map (symbol pointer as key)
    symbol_info_.get()[&symbol] = PreambleSymbolInfo{
        .def_loc = {.uri = file_uri, .range = *definition_range}};
  }

  // Helper to traverse type members (struct/union/class/enum fields)
  void TraverseTypeMembers(const slang::ast::Type& type) {
    // Slang's getCanonicalType() already unwraps all aliases
    const auto& canonical = type.getCanonicalType();

    // Check if this is a type with members (struct, union, class, enum)
    if (canonical.isStruct() || canonical.isUnion() || canonical.isClass() ||
        canonical.isEnum()) {
      // These types implement Scope interface - visit all members
      const auto& scope = canonical.as<slang::ast::Scope>();
      for (const auto& member : scope.members()) {
        member.visit(*this);
      }
    }
  }

  template <typename T>
  void handle(const T& node) {
    // For symbols, process and store their info
    if constexpr (std::is_base_of_v<slang::ast::Symbol, T>) {
      ProcessSymbol(node);

      // For type aliases, also traverse into the type's members
      if constexpr (std::is_same_v<T, slang::ast::TypeAliasType>) {
        TraverseTypeMembers(node.targetType.getType());
      }
    }
    // Always recurse to children
    this->visitDefault(node);
  }

 private:
  std::reference_wrapper<
      std::unordered_map<const slang::ast::Symbol*, PreambleSymbolInfo>>
      symbol_info_;
  std::reference_wrapper<const slang::SourceManager> source_manager_;
  std::shared_ptr<spdlog::logger> logger_;
};

}  // namespace

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

  // Prepare preprocessor options from layout service
  slang::Bag options;
  slang::parsing::PreprocessorOptions pp_options;

  // Disable implicit net declarations for stricter diagnostics
  pp_options.initialDefaultNetType = slang::parsing::TokenKind::Unknown;

  // Configure lexer options for compatibility
  slang::parsing::LexerOptions lexer_options;
  // Enable legacy protection directives for compatibility with older codebases
  lexer_options.enableLegacyProtect = true;
  options.set(lexer_options);

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
  // NOTE: We do NOT use LintMode here because it marks all scopes as
  // uninstantiated, which suppresses diagnostics inside generate blocks.
  // LanguageServerMode provides sufficient support for single-file analysis.
  comp_options.flags |= slang::ast::CompilationFlags::LanguageServerMode;
  // Set unlimited error limit for LSP - users need to see all diagnostics
  comp_options.errorLimit = 0;
  options.set(comp_options);

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

  // Extract package metadata using safe Slang API (NO getRoot() call!)
  auto packages = preamble_compilation_->getPackages();
  logger_->debug("PreambleManager: Extracting {} packages", packages.size());

  packages_.clear();
  packages_.reserve(packages.size());

  // Build package_map_ for cross-compilation binding
  package_map_.clear();

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

    // Store PackageSymbol* for cross-compilation binding
    package_map_[std::string(package->name)] = package;
  }

  // Build symbol_info_ by traversing ALL package symbols
  logger_->debug("PreambleManager: Building symbol info table");
  symbol_info_.clear();
  PreambleSymbolVisitor visitor(symbol_info_, *source_manager_, logger_);
  for (const auto* pkg : packages) {
    if (pkg != nullptr) {
      pkg->visit(visitor);  // Visitor automatically recurses
    }
  }

  logger_->debug(
      "PreambleManager: Indexed {} package symbols", symbol_info_.size());

  // Extract interface metadata using safe Slang API
  auto definitions = preamble_compilation_->getDefinitions();
  logger_->debug(
      "PreambleManager: Extracting interfaces from {} definitions",
      definitions.size());

  interfaces_.clear();
  interfaces_.reserve(definitions.size());  // Upper bound estimate

  for (const auto* symbol : definitions) {
    if (symbol == nullptr) {
      continue;
    }

    // Check if this symbol is a DefinitionSymbol and if it's an interface
    if (symbol->kind == slang::ast::SymbolKind::Definition) {
      const auto& definition = symbol->as<slang::ast::DefinitionSymbol>();

      if (definition.definitionKind == slang::ast::DefinitionKind::Interface) {
        std::string interface_name = std::string(definition.name);
        auto file_path_str = source_manager_->getFileName(definition.location);
        CanonicalPath interface_file_path(file_path_str);

        interfaces_.push_back(
            {.name = std::move(interface_name),
             .file_path = std::move(interface_file_path)});
      }
    }
  }

  // Extract module metadata using safe Slang API
  logger_->debug("PreambleManager: Extracting modules from definitions");

  modules_.clear();
  modules_.reserve(definitions.size());  // Upper bound estimate

  for (const auto* symbol : definitions) {
    if (symbol == nullptr) {
      continue;
    }

    // Check if this symbol is a DefinitionSymbol and if it's a module
    if (symbol->kind == slang::ast::SymbolKind::Definition) {
      const auto& definition = symbol->as<slang::ast::DefinitionSymbol>();

      if (definition.definitionKind == slang::ast::DefinitionKind::Module) {
        std::string module_name = std::string(definition.name);
        auto file_path_str = source_manager_->getFileName(definition.location);
        CanonicalPath module_file_path(file_path_str);

        // Extract definition range from module declaration syntax and convert
        // to LSP
        lsp::Range definition_range_lsp{};
        if (const auto* syntax = definition.getSyntax()) {
          if (syntax->kind == slang::syntax::SyntaxKind::ModuleDeclaration) {
            const auto& module_syntax =
                syntax->as<slang::syntax::ModuleDeclarationSyntax>();
            if (module_syntax.header != nullptr) {
              slang::SourceRange slang_range =
                  module_syntax.header->name.range();
              definition_range_lsp =
                  ConvertSlangRangeToLspRange(slang_range, *source_manager_);
            }
          }
        }

        // Extract parameters and convert to LSP coordinates
        std::vector<ParameterInfo> parameters;
        parameters.reserve(definition.parameters.size());
        for (const auto& param : definition.parameters) {
          auto end_offset = param.location.offset() + param.name.length();
          auto end_loc =
              slang::SourceLocation(param.location.buffer(), end_offset);
          slang::SourceRange slang_range(param.location, end_loc);
          lsp::Range param_range_lsp =
              ConvertSlangRangeToLspRange(slang_range, *source_manager_);
          parameters.push_back(
              {.name = std::string(param.name), .def_range = param_range_lsp});
        }

        // Extract ports (ANSI ports only - non-ANSI ports not yet supported)
        // and convert to LSP coordinates
        std::vector<PortInfo> ports;
        if (definition.portList != nullptr &&
            definition.portList->kind ==
                slang::syntax::SyntaxKind::AnsiPortList) {
          const auto& ansi_port_list =
              definition.portList->as<slang::syntax::AnsiPortListSyntax>();
          for (const auto* port : ansi_port_list.ports) {
            if (port != nullptr &&
                port->kind == slang::syntax::SyntaxKind::ImplicitAnsiPort) {
              const auto& implicit_port =
                  port->as<slang::syntax::ImplicitAnsiPortSyntax>();
              if (implicit_port.declarator != nullptr &&
                  implicit_port.declarator->name.valueText().length() > 0) {
                slang::SourceRange slang_range =
                    implicit_port.declarator->name.range();
                lsp::Range port_range_lsp =
                    ConvertSlangRangeToLspRange(slang_range, *source_manager_);
                ports.push_back(
                    {.name = std::string(
                         implicit_port.declarator->name.valueText()),
                     .def_range = port_range_lsp});
              }
            }
          }
        }

        modules_.push_back(
            {.name = std::move(module_name),
             .file_path = std::move(module_file_path),
             .def_range = definition_range_lsp,
             .ports = std::move(ports),
             .parameters = std::move(parameters),
             .port_lookup = {},
             .parameter_lookup = {}});
      }
    }
  }

  // Build module lookup map for O(1) access
  module_lookup_.clear();
  for (auto& module : modules_) {
    module_lookup_[module.name] = &module;

    // Build port lookup map for O(1) access
    for (const auto& port : module.ports) {
      module.port_lookup[port.name] = &port;
    }

    // Build parameter lookup map for O(1) access
    for (const auto& param : module.parameters) {
      module.parameter_lookup[param.name] = &param;
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

auto PreambleManager::GetPackages() const -> const std::vector<PackageInfo>& {
  return packages_;
}

auto PreambleManager::GetInterfaces() const
    -> const std::vector<InterfaceInfo>& {
  return interfaces_;
}

auto PreambleManager::GetModules() const -> const std::vector<ModuleInfo>& {
  return modules_;
}

auto PreambleManager::GetModule(std::string_view name) const
    -> const ModuleInfo* {
  auto it = module_lookup_.find(std::string(name));
  if (it != module_lookup_.end()) {
    return it->second;
  }
  return nullptr;
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
  auto it = package_map_.find(std::string(name));
  if (it != package_map_.end()) {
    return it->second;
  }
  return nullptr;
}

auto PreambleManager::IsPreambleSymbol(const slang::ast::Symbol* symbol) const
    -> bool {
  return symbol_info_.contains(symbol);
}

auto PreambleManager::GetSymbolInfo(const slang::ast::Symbol* symbol) const
    -> std::optional<PreambleSymbolInfo> {
  auto it = symbol_info_.find(symbol);
  if (it != symbol_info_.end()) {
    return it->second;
  }
  return std::nullopt;
}

}  // namespace slangd::services

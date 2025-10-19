#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <slang/text/SourceLocation.h>
#include <spdlog/spdlog.h>

#include "lsp/basic.hpp"
#include "slangd/core/project_layout_service.hpp"
#include "slangd/utils/canonical_path.hpp"

// Forward declarations
namespace slang {
namespace ast {
class Compilation;
class DefinitionSymbol;
class PackageSymbol;
class Symbol;
}  // namespace ast
class SourceManager;
}  // namespace slang

namespace slangd::semantic {
class DefinitionExtractor;
}

namespace slangd::services {

// Package entry: combines symbol pointer with cached metadata for fast
// filtering
struct PackageEntry {
  const slang::ast::PackageSymbol* symbol = nullptr;
  CanonicalPath file_path;
};

// Interface entry: combines definition symbol with cached metadata
struct InterfaceEntry {
  const slang::ast::Symbol* definition = nullptr;
  CanonicalPath file_path;
};

// Port metadata extracted from module definitions
struct PortInfo {
  std::string name;
  lsp::Range def_range;
};

// Parameter metadata extracted from module definitions
struct ParameterInfo {
  std::string name;
  lsp::Range def_range;
};

// Module metadata extracted from preamble compilation
struct ModuleInfo {
  std::string name;
  CanonicalPath file_path;
  lsp::Range def_range;
  std::vector<PortInfo> ports;
  std::vector<ParameterInfo> parameters;
  const slang::ast::Symbol* definition =
      nullptr;  // Symbol pointer for cross-compilation

  // O(1) lookups (built during extraction in BuildFromLayout)
  std::unordered_map<std::string, const PortInfo*> port_lookup;
  std::unordered_map<std::string, const ParameterInfo*> parameter_lookup;
};

// PreambleManager: Immutable snapshot of package/interface metadata from
// preamble compilation. Use CreateFromProjectLayout() factory method for
// convenience.
class PreambleManager {
 public:
  // Default constructor
  PreambleManager() = default;

  // Factory method - convenient way to create and initialize a PreambleManager
  // Creates a preamble compilation from all project files and extracts metadata
  [[nodiscard]] static auto CreateFromProjectLayout(
      std::shared_ptr<ProjectLayoutService> layout_service,
      std::shared_ptr<spdlog::logger> logger = nullptr)
      -> std::shared_ptr<PreambleManager>;

  // Non-copyable, non-movable
  PreambleManager(const PreambleManager&) = delete;
  PreambleManager(PreambleManager&&) = delete;
  auto operator=(const PreambleManager&) -> PreambleManager& = delete;
  auto operator=(PreambleManager&&) -> PreambleManager& = delete;
  ~PreambleManager() = default;

  // Accessors for preamble_manager data
  [[nodiscard]] auto GetPackages() const
      -> const std::unordered_map<std::string, PackageEntry>&;
  [[nodiscard]] auto GetInterfaces() const
      -> const std::unordered_map<std::string, InterfaceEntry>&;
  [[nodiscard]] auto GetModules() const -> const std::vector<ModuleInfo>&;
  [[nodiscard]] auto GetModule(std::string_view name) const
      -> const ModuleInfo*;

  // Symbol access for cross-compilation binding (convenience methods)
  [[nodiscard]] auto GetPackage(std::string_view name) const
      -> const slang::ast::PackageSymbol*;
  [[nodiscard]] auto GetInterfaceDefinition(std::string_view name) const
      -> const slang::ast::Symbol*;
  [[nodiscard]] auto GetModuleDefinition(std::string_view name) const
      -> const slang::ast::Symbol*;

  // Include directories and defines from ProjectLayoutService
  [[nodiscard]] auto GetIncludeDirectories() const
      -> const std::vector<CanonicalPath>&;
  [[nodiscard]] auto GetDefines() const -> const std::vector<std::string>&;

  // SourceManager accessor for resolving cross-file buffer IDs
  [[nodiscard]] auto GetSourceManager() const -> const slang::SourceManager&;

  // Compilation accessor for symbol compilation checking
  [[nodiscard]] auto GetCompilation() const -> const slang::ast::Compilation&;

  // Version tracking for cache invalidation
  [[nodiscard]] auto GetVersion() const -> uint64_t;

  // Build preamble_manager from ProjectLayoutService - public method
  auto BuildFromLayout(
      std::shared_ptr<ProjectLayoutService> layout_service,
      std::shared_ptr<spdlog::logger> logger) -> void;

 private:
  // Unified storage: symbol pointer + cached metadata for filtering
  std::unordered_map<std::string, PackageEntry> packages_;
  std::unordered_map<std::string, InterfaceEntry> interfaces_;

  // Module storage keeps ModuleInfo for port/parameter metadata (LSP needs it)
  std::vector<ModuleInfo> modules_;
  std::unordered_map<std::string, const ModuleInfo*> module_lookup_;

  std::vector<CanonicalPath> include_directories_;
  std::vector<std::string> defines_;
  uint64_t version_ = 1;

  // Preamble compilation objects
  std::shared_ptr<slang::ast::Compilation> preamble_compilation_;
  std::shared_ptr<slang::SourceManager> source_manager_;

  // Logger
  std::shared_ptr<spdlog::logger> logger_;
};

}  // namespace slangd::services

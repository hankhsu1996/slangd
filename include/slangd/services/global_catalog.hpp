#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <slang/text/SourceLocation.h>
#include <spdlog/spdlog.h>

#include "slangd/core/project_layout_service.hpp"
#include "slangd/utils/canonical_path.hpp"

// Forward declarations
namespace slang {
namespace ast {
class Compilation;
}
class SourceManager;
}  // namespace slang

namespace slangd::services {

// Package metadata extracted from global compilation
struct PackageInfo {
  std::string name;
  CanonicalPath file_path;
  // Future: additional metadata like exported symbols
};

// Interface metadata for future implementation
struct InterfaceInfo {
  std::string name;
  CanonicalPath file_path;
  // Future: additional metadata like modports
};

// Port metadata extracted from module definitions
struct PortInfo {
  std::string name;
  slang::SourceRange def_range;
};

// Parameter metadata extracted from module definitions
struct ParameterInfo {
  std::string name;
  slang::SourceRange def_range;
};

// Module metadata extracted from global compilation
struct ModuleInfo {
  std::string name;
  CanonicalPath file_path;
  slang::SourceRange definition_range;
  std::vector<PortInfo> ports;
  std::vector<ParameterInfo> parameters;

  // O(1) lookups (built during extraction in BuildFromLayout)
  std::unordered_map<std::string, const PortInfo*> port_lookup;
  std::unordered_map<std::string, const ParameterInfo*> parameter_lookup;
};

// GlobalCatalog: Immutable snapshot of package/interface metadata from global
// compilation. Use CreateFromProjectLayout() factory method for convenience.
class GlobalCatalog {
 public:
  // Default constructor
  GlobalCatalog() = default;

  // Factory method - convenient way to create and initialize a GlobalCatalog
  // Creates a global compilation from all project files and extracts metadata
  [[nodiscard]] static auto CreateFromProjectLayout(
      std::shared_ptr<ProjectLayoutService> layout_service,
      std::shared_ptr<spdlog::logger> logger = nullptr)
      -> std::shared_ptr<GlobalCatalog>;

  // Non-copyable, non-movable
  GlobalCatalog(const GlobalCatalog&) = delete;
  GlobalCatalog(GlobalCatalog&&) = delete;
  auto operator=(const GlobalCatalog&) -> GlobalCatalog& = delete;
  auto operator=(GlobalCatalog&&) -> GlobalCatalog& = delete;
  ~GlobalCatalog() = default;

  // Accessors for catalog data
  [[nodiscard]] auto GetPackages() const -> const std::vector<PackageInfo>&;
  [[nodiscard]] auto GetInterfaces() const -> const std::vector<InterfaceInfo>&;
  [[nodiscard]] auto GetModules() const -> const std::vector<ModuleInfo>&;
  [[nodiscard]] auto GetModule(std::string_view name) const
      -> const ModuleInfo*;

  // Include directories and defines from ProjectLayoutService
  [[nodiscard]] auto GetIncludeDirectories() const
      -> const std::vector<CanonicalPath>&;
  [[nodiscard]] auto GetDefines() const -> const std::vector<std::string>&;

  // SourceManager accessor for resolving cross-file buffer IDs
  [[nodiscard]] auto GetSourceManager() const -> const slang::SourceManager&;

  // Version tracking for cache invalidation
  [[nodiscard]] auto GetVersion() const -> uint64_t;

  // Build catalog from ProjectLayoutService - public method
  auto BuildFromLayout(
      std::shared_ptr<ProjectLayoutService> layout_service,
      std::shared_ptr<spdlog::logger> logger) -> void;

 private:
  // Catalog data
  std::vector<PackageInfo> packages_;
  std::vector<InterfaceInfo> interfaces_;  // Empty for MVP
  std::vector<ModuleInfo> modules_;
  std::unordered_map<std::string, const ModuleInfo*> module_lookup_;
  std::vector<CanonicalPath> include_directories_;
  std::vector<std::string> defines_;
  uint64_t version_ = 1;

  // Global compilation objects
  std::shared_ptr<slang::ast::Compilation> global_compilation_;
  std::shared_ptr<slang::SourceManager> source_manager_;

  // Logger
  std::shared_ptr<spdlog::logger> logger_;
};

}  // namespace slangd::services

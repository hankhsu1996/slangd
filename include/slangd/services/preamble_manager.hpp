#pragma once

#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>
#include <slang/text/SourceLocation.h>
#include <slang/util/Hash.h>
#include <spdlog/spdlog.h>

#include "slangd/core/project_layout_service.hpp"
#include "slangd/utils/canonical_path.hpp"

// Forward declarations
namespace slang {
namespace ast {
class Compilation;
class DefinitionSymbol;
class PackageSymbol;
class Scope;
class Symbol;
}  // namespace ast
class SourceManager;
}  // namespace slang

namespace slangd::semantic {
class DefinitionExtractor;
}

namespace slangd::services {

// PreambleManager: Immutable snapshot of preamble compilation.
// Provides direct access to Slang Compilation's symbol collections.
// Use CreateFromProjectLayout() factory method for convenience.
class PreambleManager {
 public:
  // Default constructor
  PreambleManager() = default;

  // Factory method - convenient way to create and initialize a PreambleManager
  // Creates a preamble compilation from all project files and extracts metadata
  [[nodiscard]] static auto CreateFromProjectLayout(
      std::shared_ptr<ProjectLayoutService> layout_service,
      asio::any_io_executor compilation_executor,
      std::shared_ptr<spdlog::logger> logger = spdlog::default_logger())
      -> asio::awaitable<std::shared_ptr<PreambleManager>>;

  // Non-copyable, non-movable
  PreambleManager(const PreambleManager&) = delete;
  PreambleManager(PreambleManager&&) = delete;
  auto operator=(const PreambleManager&) -> PreambleManager& = delete;
  auto operator=(PreambleManager&&) -> PreambleManager& = delete;
  ~PreambleManager() = default;

  // Direct access to Compilation's internal maps
  [[nodiscard]] auto GetPackageMap() const -> const
      slang::flat_hash_map<std::string_view, const slang::ast::PackageSymbol*>&;
  [[nodiscard]] auto GetDefinitionMap() const -> const slang::flat_hash_map<
      std::tuple<std::string_view, const slang::ast::Scope*>,
      std::pair<std::vector<const slang::ast::Symbol*>, bool>>&;

  // Include directories and defines from ProjectLayoutService
  [[nodiscard]] auto GetIncludeDirectories() const
      -> const std::vector<CanonicalPath>&;
  [[nodiscard]] auto GetDefines() const -> const std::vector<std::string>&;

  // SourceManager accessor for resolving cross-file buffer IDs
  [[nodiscard]] auto GetSourceManager() const -> const slang::SourceManager&;

  // Compilation accessor for symbol compilation checking
  [[nodiscard]] auto GetCompilation() const -> const slang::ast::Compilation&;

  // Build preamble_manager from ProjectLayoutService - public method
  auto BuildFromLayout(
      std::shared_ptr<ProjectLayoutService> layout_service,
      std::shared_ptr<spdlog::logger> logger,
      asio::any_io_executor compilation_executor) -> asio::awaitable<void>;

 private:
  std::vector<CanonicalPath> include_directories_;
  std::vector<std::string> defines_;

  // Preamble compilation objects
  std::shared_ptr<slang::ast::Compilation> preamble_compilation_;
  std::shared_ptr<slang::SourceManager> source_manager_;

  // Logger
  std::shared_ptr<spdlog::logger> logger_;
};

}  // namespace slangd::services

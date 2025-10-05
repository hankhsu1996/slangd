#pragma once

#include <memory>
#include <string>

#include <slang/ast/Compilation.h>
#include <slang/text/SourceManager.h>
#include <spdlog/spdlog.h>

#include "slangd/core/project_layout_service.hpp"
#include "slangd/semantic/semantic_index.hpp"
#include "slangd/services/global_catalog.hpp"

namespace slangd::services {

// Per-request compilation session for LSP queries
// Creates fresh Slang compilation with current buffer + optional catalog files
// Provides symbol indexing for go-to-definition and document symbols
class OverlaySession {
 public:
  // Factory method for creating overlay sessions
  // catalog can be nullptr - session will work in single-file mode
  static auto Create(
      std::string uri, std::string content,
      std::shared_ptr<ProjectLayoutService> layout_service,
      std::shared_ptr<const GlobalCatalog> catalog = nullptr,
      std::shared_ptr<spdlog::logger> logger = nullptr)
      -> std::shared_ptr<OverlaySession>;

  // Move-only type for performance
  OverlaySession(const OverlaySession&) = delete;
  OverlaySession(OverlaySession&&) = default;
  auto operator=(const OverlaySession&) -> OverlaySession& = delete;
  auto operator=(OverlaySession&&) -> OverlaySession& = default;
  ~OverlaySession() = default;

  // Access to unified semantic index for LSP queries
  [[nodiscard]] auto GetSemanticIndex() const
      -> const semantic::SemanticIndex& {
    return *semantic_index_;
  }

  // Access to compilation for advanced queries (used for diagnostic extraction)
  [[nodiscard]] auto GetCompilation() const -> slang::ast::Compilation& {
    return *compilation_;
  }

  // Access to source manager for location mapping
  [[nodiscard]] auto GetSourceManager() const -> const slang::SourceManager& {
    return *source_manager_;
  }

 private:
  // Private constructor - use Create() factory method
  OverlaySession(
      std::shared_ptr<slang::SourceManager> source_manager,
      std::unique_ptr<slang::ast::Compilation> compilation,
      std::unique_ptr<semantic::SemanticIndex> semantic_index,
      std::shared_ptr<spdlog::logger> logger);

  // Build fresh compilation with current buffer and optional catalog files
  static auto BuildCompilation(
      std::string uri, std::string content,
      std::shared_ptr<ProjectLayoutService> layout_service,
      std::shared_ptr<const GlobalCatalog> catalog,
      std::shared_ptr<spdlog::logger> logger)
      -> std::tuple<
          std::shared_ptr<slang::SourceManager>,
          std::unique_ptr<slang::ast::Compilation>>;

  // Core session components
  std::shared_ptr<slang::SourceManager> source_manager_;
  std::unique_ptr<slang::ast::Compilation> compilation_;
  std::unique_ptr<semantic::SemanticIndex> semantic_index_;
  std::shared_ptr<spdlog::logger> logger_;
};

}  // namespace slangd::services

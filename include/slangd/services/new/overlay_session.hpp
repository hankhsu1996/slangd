#pragma once

#include <memory>
#include <string>

#include <slang/ast/Compilation.h>
#include <slang/text/SourceManager.h>
#include <spdlog/spdlog.h>

#include "slangd/core/global_catalog.hpp"
#include "slangd/core/project_layout_service.hpp"
#include "slangd/semantic/definition_index.hpp"
#include "slangd/semantic/diagnostic_index.hpp"
#include "slangd/semantic/symbol_index.hpp"

namespace slangd::services::overlay {

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
      -> std::unique_ptr<OverlaySession>;

  // Move-only type for performance
  OverlaySession(const OverlaySession&) = delete;
  OverlaySession(OverlaySession&&) = default;
  auto operator=(const OverlaySession&) -> OverlaySession& = delete;
  auto operator=(OverlaySession&&) -> OverlaySession& = default;
  ~OverlaySession() = default;

  // Access to symbol index for LSP queries
  [[nodiscard]] auto GetDefinitionIndex() const
      -> const semantic::DefinitionIndex& {
    return *definition_index_;
  }

  // Access to diagnostic index for LSP diagnostics
  [[nodiscard]] auto GetDiagnosticIndex() const
      -> const semantic::DiagnosticIndex& {
    return *diagnostic_index_;
  }

  // Access to symbol index for document symbols
  [[nodiscard]] auto GetSymbolIndex() const -> const semantic::SymbolIndex& {
    return *symbol_index_;
  }

  // Access to compilation for advanced queries
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
      std::unique_ptr<semantic::DefinitionIndex> definition_index,
      std::unique_ptr<semantic::DiagnosticIndex> diagnostic_index,
      std::unique_ptr<semantic::SymbolIndex> symbol_index,
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
  std::unique_ptr<semantic::DefinitionIndex> definition_index_;
  std::unique_ptr<semantic::DiagnosticIndex> diagnostic_index_;
  std::unique_ptr<semantic::SymbolIndex> symbol_index_;
  std::shared_ptr<spdlog::logger> logger_;
};

}  // namespace slangd::services::overlay

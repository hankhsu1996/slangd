#pragma once

#include <memory>
#include <string>

#include <slang/ast/Compilation.h>
#include <slang/text/SourceManager.h>
#include <spdlog/spdlog.h>

#include "slangd/core/project_layout_service.hpp"
#include "slangd/semantic/semantic_index.hpp"
#include "slangd/services/preamble_manager.hpp"

namespace slangd::services {

// Forward declaration for friend
class LanguageService;

// Compilation session with current buffer + preamble_manager files for LSP
// queries
class OverlaySession {
 public:
  friend class LanguageService;

  static auto Create(
      std::string uri, std::string content,
      std::shared_ptr<ProjectLayoutService> layout_service,
      std::shared_ptr<const PreambleManager> preamble_manager = nullptr,
      std::shared_ptr<spdlog::logger> logger = nullptr)
      -> std::shared_ptr<OverlaySession>;

  // Core compilation building logic (used by Create and parse diagnostics)
  // Pass preamble_manager=nullptr for single-file mode
  static auto BuildCompilation(
      std::string uri, std::string content,
      std::shared_ptr<ProjectLayoutService> layout_service,
      std::shared_ptr<const PreambleManager> preamble_manager,
      std::shared_ptr<spdlog::logger> logger)
      -> std::tuple<
          std::shared_ptr<slang::SourceManager>,
          std::unique_ptr<slang::ast::Compilation>, slang::BufferID>;

  // Create session from pre-built compilation and index (for two-phase
  // creation)
  static auto CreateFromParts(
      std::shared_ptr<slang::SourceManager> source_manager,
      std::shared_ptr<slang::ast::Compilation> compilation,
      std::unique_ptr<semantic::SemanticIndex> semantic_index,
      slang::BufferID main_buffer_id, std::shared_ptr<spdlog::logger> logger,
      std::shared_ptr<const PreambleManager> preamble_manager = nullptr)
      -> std::shared_ptr<OverlaySession>;

  OverlaySession(const OverlaySession&) = delete;
  OverlaySession(OverlaySession&&) = default;
  auto operator=(const OverlaySession&) -> OverlaySession& = delete;
  auto operator=(OverlaySession&&) noexcept -> OverlaySession& = default;
  ~OverlaySession() = default;

  [[nodiscard]] auto GetSemanticIndex() const
      -> const semantic::SemanticIndex& {
    return *semantic_index_;
  }

  [[nodiscard]] auto GetCompilation() const -> slang::ast::Compilation& {
    return *compilation_;
  }

  [[nodiscard]] auto GetSourceManager() const -> const slang::SourceManager& {
    return *source_manager_;
  }

  // Shared pointer accessors for async lifetime management in CompilationState.
  // Prefer reference accessors above for most use cases.
  [[nodiscard]] auto GetCompilationPtr() const
      -> std::shared_ptr<slang::ast::Compilation> {
    return compilation_;
  }

  [[nodiscard]] auto GetSourceManagerPtr() const
      -> std::shared_ptr<slang::SourceManager> {
    return source_manager_;
  }

  [[nodiscard]] auto GetMainBufferID() const -> slang::BufferID {
    return main_buffer_id_;
  }

 private:
  OverlaySession(
      std::shared_ptr<slang::SourceManager> source_manager,
      std::shared_ptr<slang::ast::Compilation> compilation,
      std::unique_ptr<semantic::SemanticIndex> semantic_index,
      slang::BufferID main_buffer_id, std::shared_ptr<spdlog::logger> logger,
      std::shared_ptr<const PreambleManager> preamble_manager = nullptr);

  std::shared_ptr<slang::SourceManager> source_manager_;
  std::shared_ptr<slang::ast::Compilation> compilation_;
  std::unique_ptr<semantic::SemanticIndex> semantic_index_;
  slang::BufferID main_buffer_id_;
  std::shared_ptr<spdlog::logger> logger_;
  std::shared_ptr<const PreambleManager> preamble_manager_;
};

}  // namespace slangd::services

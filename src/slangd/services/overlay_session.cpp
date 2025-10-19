#include "slangd/services/overlay_session.hpp"

#include <fmt/format.h>
#include <slang/ast/Compilation.h>
#include <slang/parsing/Preprocessor.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/util/Bag.h>

#include "slangd/semantic/semantic_index.hpp"
#include "slangd/utils/canonical_path.hpp"
#include "slangd/utils/compilation_options.hpp"
#include "slangd/utils/scoped_timer.hpp"

namespace slangd::services {

namespace {

// PreambleAwareCompilation: Subclass for cross-compilation symbol binding
// Directly populates protected packageMap with preamble PackageSymbol pointers
// Note: getPackage() is NOT virtual, so we cannot override it. Instead, we
// populate the packageMap directly, which getPackage() uses for lookups.
class PreambleAwareCompilation : public slang::ast::Compilation {
 public:
  PreambleAwareCompilation(
      const slang::Bag& options,
      std::shared_ptr<const PreambleManager> preamble_manager,
      const CanonicalPath& current_file_path)
      : Compilation(options), preamble_manager_(std::move(preamble_manager)) {
    // Populate packageMap with preamble packages (direct injection)
    // Enables cross-compilation: overlay can reference preamble symbols
    for (const auto& [name, entry] : preamble_manager_->GetPackages()) {
      // Skip if this package is defined in current file (deduplication)
      // Let overlay's version be used instead of preamble's
      if (entry.file_path.Path() == current_file_path.Path()) {
        continue;
      }

      packageMap[entry.symbol->name] = entry.symbol;
    }
  }

 private:
  // Keep preamble alive for the lifetime of this compilation
  std::shared_ptr<const PreambleManager> preamble_manager_;
};

}  // anonymous namespace

auto OverlaySession::Create(
    std::string uri, std::string content,
    std::shared_ptr<ProjectLayoutService> layout_service,
    std::shared_ptr<const PreambleManager> preamble_manager,
    std::shared_ptr<spdlog::logger> logger) -> std::shared_ptr<OverlaySession> {
  if (!logger) {
    logger = spdlog::default_logger();
  }

  utils::ScopedTimer timer("OverlaySession creation", logger);
  logger->debug("Creating overlay session for: {}", uri);

  // Build fresh compilation with current buffer and optional preamble_manager
  // files
  auto [source_manager, compilation, main_buffer_id] =
      BuildCompilation(uri, content, layout_service, preamble_manager, logger);

  // Create unified semantic index (replaces DefinitionIndex + SymbolIndex)
  // Note: FromCompilation calls forceElaborate() which populates
  // compilation.diagMap Diagnostics are extracted on-demand via
  // ComputeDiagnostics()
  auto result = semantic::SemanticIndex::FromCompilation(
      *compilation, *source_manager, uri, preamble_manager.get(), logger);

  if (!result) {
    logger->error(
        "Failed to build semantic index for '{}': {}", uri, result.error());
    // Return nullptr - semantic index features disabled for this file
    // LSP server continues for other files
    return nullptr;
  }

  auto semantic_index = std::move(*result);

  auto elapsed = timer.GetElapsed();
  auto entry_count = semantic_index->GetSemanticEntries().size();
  logger->debug(
      "Overlay session created with {} semantic entries ({})", entry_count,
      utils::ScopedTimer::FormatDuration(elapsed));

  return std::shared_ptr<OverlaySession>(new OverlaySession(
      std::move(source_manager),
      std::shared_ptr<slang::ast::Compilation>(std::move(compilation)),
      std::move(semantic_index), main_buffer_id, logger, preamble_manager));
}

auto OverlaySession::CreateFromParts(
    std::shared_ptr<slang::SourceManager> source_manager,
    std::shared_ptr<slang::ast::Compilation> compilation,
    std::unique_ptr<semantic::SemanticIndex> semantic_index,
    slang::BufferID main_buffer_id, std::shared_ptr<spdlog::logger> logger,
    std::shared_ptr<const PreambleManager> preamble_manager)
    -> std::shared_ptr<OverlaySession> {
  return std::shared_ptr<OverlaySession>(new OverlaySession(
      std::move(source_manager), std::move(compilation),
      std::move(semantic_index), main_buffer_id, logger, preamble_manager));
}

OverlaySession::OverlaySession(
    std::shared_ptr<slang::SourceManager> source_manager,
    std::shared_ptr<slang::ast::Compilation> compilation,
    std::unique_ptr<semantic::SemanticIndex> semantic_index,
    slang::BufferID main_buffer_id, std::shared_ptr<spdlog::logger> logger,
    std::shared_ptr<const PreambleManager> preamble_manager)
    : source_manager_(std::move(source_manager)),
      compilation_(std::move(compilation)),
      semantic_index_(std::move(semantic_index)),
      main_buffer_id_(main_buffer_id),
      logger_(std::move(logger)),
      preamble_manager_(std::move(preamble_manager)) {
}

auto OverlaySession::BuildCompilation(
    std::string uri, std::string content,
    std::shared_ptr<ProjectLayoutService> layout_service,
    std::shared_ptr<const PreambleManager> preamble_manager,
    std::shared_ptr<spdlog::logger> logger)
    -> std::tuple<
        std::shared_ptr<slang::SourceManager>,
        std::unique_ptr<slang::ast::Compilation>, slang::BufferID> {
  if (!logger) {
    logger = spdlog::default_logger();
  }

  utils::ScopedTimer timer(fmt::format("BuildCompilation [{}]", uri), logger);

  // Create fresh source manager
  auto source_manager = std::make_shared<slang::SourceManager>();

  // Start with standard LSP compilation options
  auto options = utils::CreateLspCompilationOptions();

  // Add project-specific preprocessor options
  if (layout_service) {
    auto pp_options =
        options.getOrDefault<slang::parsing::PreprocessorOptions>();
    for (const auto& include_dir : layout_service->GetIncludeDirectories()) {
      pp_options.additionalIncludePaths.emplace_back(include_dir.Path());
    }
    for (const auto& define : layout_service->GetDefines()) {
      pp_options.predefines.push_back(define);
    }
    options.set(pp_options);

    logger->debug(
        "Applied {} include dirs, {} defines",
        layout_service->GetIncludeDirectories().size(),
        layout_service->GetDefines().size());
  }

  // Get file path for deduplication (needed before creating compilation)
  auto file_path = CanonicalPath::FromUri(uri);

  // Create compilation with options
  // Use PreambleAwareCompilation when preamble available for cross-compilation
  std::unique_ptr<slang::ast::Compilation> compilation;
  if (preamble_manager) {
    compilation = std::make_unique<PreambleAwareCompilation>(
        options, preamble_manager, file_path);
    logger->debug(
        "Created PreambleAwareCompilation with {} packages",
        preamble_manager->GetPackages().size());
  } else {
    compilation = std::make_unique<slang::ast::Compilation>(options);
  }

  // Add current buffer content (authoritative)
  auto buffer = source_manager->assignText(file_path.String(), content);
  auto main_buffer_id = buffer.id;
  auto buffer_tree =
      slang::syntax::SyntaxTree::fromBuffer(buffer, *source_manager, options);

  if (buffer_tree) {
    compilation->addSyntaxTree(buffer_tree);
  } else {
    logger->error(
        "Failed to create syntax tree for buffer: {}",
        file_path.Path().string());
  }

  // Add files from preamble manager if available
  if (preamble_manager) {
    // NOTE: Packages are NOT loaded as syntax trees!
    // PreambleAwareCompilation injects preamble PackageSymbol* pointers
    // directly into packageMap for cross-compilation binding.
    // This eliminates duplicate package loading per session.

    // Add interfaces from preamble manager
    for (const auto& [name, entry] : preamble_manager->GetInterfaces()) {
      // Skip if this is the same file as our buffer (deduplication)
      if (entry.file_path.Path() == file_path.Path()) {
        logger->debug(
            "Skipping buffer file from preamble manager: {}",
            entry.file_path.Path().string());
        continue;
      }

      auto interface_tree_result = slang::syntax::SyntaxTree::fromFile(
          entry.file_path.Path().string(), *source_manager, options);
      if (interface_tree_result) {
        compilation->addSyntaxTree(interface_tree_result.value());
      }
    }

  } else {
    logger->debug("No preamble manager provided - single-file mode");
  }

  return std::make_tuple(
      std::move(source_manager), std::move(compilation), main_buffer_id);
}

}  // namespace slangd::services

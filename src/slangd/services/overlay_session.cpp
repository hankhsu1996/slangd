#include "slangd/services/overlay_session.hpp"

#include <slang/ast/Compilation.h>
#include <slang/parsing/Preprocessor.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/util/Bag.h>

#include "slangd/semantic/semantic_index.hpp"
#include "slangd/utils/canonical_path.hpp"
#include "slangd/utils/scoped_timer.hpp"

namespace slangd::services {

auto OverlaySession::Create(
    std::string uri, std::string content,
    std::shared_ptr<ProjectLayoutService> layout_service,
    std::shared_ptr<const GlobalCatalog> catalog,
    std::shared_ptr<spdlog::logger> logger) -> std::shared_ptr<OverlaySession> {
  if (!logger) {
    logger = spdlog::default_logger();
  }

  utils::ScopedTimer timer("OverlaySession creation", logger);
  logger->debug("Creating overlay session for: {}", uri);

  // Build fresh compilation with current buffer and optional catalog files
  auto [source_manager, compilation, main_buffer_id] =
      BuildCompilation(uri, content, layout_service, catalog, logger);

  // Create unified semantic index (replaces DefinitionIndex + SymbolIndex)
  auto semantic_index = semantic::SemanticIndex::FromCompilation(
      *compilation, *source_manager, uri, catalog.get());

  auto elapsed = timer.GetElapsed();
  auto entry_count = semantic_index->GetSemanticEntries().size();
  logger->debug(
      "Overlay session created with {} semantic entries ({})", entry_count,
      utils::ScopedTimer::FormatDuration(elapsed));

  return std::shared_ptr<OverlaySession>(new OverlaySession(
      std::move(source_manager), std::move(compilation),
      std::move(semantic_index), main_buffer_id, logger));
}

OverlaySession::OverlaySession(
    std::shared_ptr<slang::SourceManager> source_manager,
    std::unique_ptr<slang::ast::Compilation> compilation,
    std::unique_ptr<semantic::SemanticIndex> semantic_index,
    slang::BufferID main_buffer_id, std::shared_ptr<spdlog::logger> logger)
    : source_manager_(std::move(source_manager)),
      compilation_(std::move(compilation)),
      semantic_index_(std::move(semantic_index)),
      main_buffer_id_(main_buffer_id),
      logger_(std::move(logger)) {
}

auto OverlaySession::BuildCompilation(
    std::string uri, std::string content,
    std::shared_ptr<ProjectLayoutService> layout_service,
    std::shared_ptr<const GlobalCatalog> catalog,
    std::shared_ptr<spdlog::logger> logger)
    -> std::tuple<
        std::shared_ptr<slang::SourceManager>,
        std::unique_ptr<slang::ast::Compilation>, slang::BufferID> {
  // Create fresh source manager
  auto source_manager = std::make_shared<slang::SourceManager>();

  // Prepare preprocessor options
  slang::Bag options;
  slang::parsing::PreprocessorOptions pp_options;

  // Apply include directories and defines from layout service
  if (layout_service) {
    for (const auto& include_dir : layout_service->GetIncludeDirectories()) {
      pp_options.additionalIncludePaths.emplace_back(include_dir.Path());
    }

    for (const auto& define : layout_service->GetDefines()) {
      pp_options.predefines.push_back(define);
    }

    logger->debug(
        "Applied {} include dirs, {} defines",
        layout_service->GetIncludeDirectories().size(),
        layout_service->GetDefines().size());
  }
  options.set(pp_options);

  // Create compilation options for LSP mode
  slang::ast::CompilationOptions comp_options;
  comp_options.flags |= slang::ast::CompilationFlags::LintMode;
  comp_options.flags |= slang::ast::CompilationFlags::LanguageServerMode;
  comp_options.flags |= slang::ast::CompilationFlags::IgnoreUnknownModules;
  options.set(comp_options);

  // Create compilation with options
  auto compilation = std::make_unique<slang::ast::Compilation>(options);

  // Add current buffer content (authoritative)
  auto file_path = CanonicalPath::FromUri(uri);
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

  // Add files from global catalog if available
  if (catalog) {
    // Add packages from catalog
    for (const auto& package_info : catalog->GetPackages()) {
      // Skip if this is the same file as our buffer (deduplication)
      if (package_info.file_path.Path() == file_path.Path()) {
        logger->debug(
            "Skipping buffer file from catalog: {}",
            package_info.file_path.Path().string());
        continue;
      }

      auto package_tree_result = slang::syntax::SyntaxTree::fromFile(
          package_info.file_path.Path().string(), *source_manager, options);
      if (package_tree_result) {
        compilation->addSyntaxTree(package_tree_result.value());
      }
    }

    // Add interfaces from catalog
    for (const auto& interface_info : catalog->GetInterfaces()) {
      // Skip if this is the same file as our buffer (deduplication)
      if (interface_info.file_path.Path() == file_path.Path()) {
        logger->debug(
            "Skipping buffer file from catalog: {}",
            interface_info.file_path.Path().string());
        continue;
      }

      auto interface_tree_result = slang::syntax::SyntaxTree::fromFile(
          interface_info.file_path.Path().string(), *source_manager, options);
      if (interface_tree_result) {
        compilation->addSyntaxTree(interface_tree_result.value());
      }
    }

  } else {
    logger->debug("No catalog provided - single-file mode");
  }

  return std::make_tuple(
      std::move(source_manager), std::move(compilation), main_buffer_id);
}

}  // namespace slangd::services

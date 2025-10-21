#pragma once

#include <algorithm>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <asio.hpp>
#include <catch2/catch_all.hpp>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include "slangd/core/project_layout_service.hpp"
#include "slangd/semantic/diagnostic_converter.hpp"
#include "slangd/services/overlay_session.hpp"
#include "slangd/services/preamble_manager.hpp"
#include "slangd/utils/canonical_path.hpp"
#include "slangd/utils/conversion.hpp"
#include "test/slangd/common/file_fixture.hpp"
#include "test/slangd/common/semantic_fixture.hpp"

namespace slangd::test {

// Extended fixture for multifile tests
class MultiFileSemanticFixture : public SemanticTestFixture,
                                 public FileTestFixture {
 public:
  MultiFileSemanticFixture() : FileTestFixture("slangd_semantic_multifile") {
  }

  ~MultiFileSemanticFixture() = default;

  // Explicitly delete copy operations
  MultiFileSemanticFixture(const MultiFileSemanticFixture&) = delete;
  auto operator=(const MultiFileSemanticFixture&)
      -> MultiFileSemanticFixture& = delete;

  // Explicitly delete move operations
  MultiFileSemanticFixture(MultiFileSemanticFixture&&) = delete;
  auto operator=(MultiFileSemanticFixture&&)
      -> MultiFileSemanticFixture& = delete;

  // Result struct for BuildIndexFromFiles - bundles index with dependencies
  struct IndexWithFiles {
    std::unique_ptr<SemanticIndex> index;
    std::shared_ptr<slang::SourceManager> source_manager;
    std::unique_ptr<slang::ast::Compilation> compilation;
    std::vector<std::string> file_paths;  // The actual file paths created
    std::string uri;
  };

  // Role-based multifile test setup for clear LSP scenarios
  // Prevents confusion about which file is being indexed from
  enum class FileRole {
    kCurrentFile,  // The file being edited (indexed from) - LSP active file
    kOpenedFile,   // Another opened file in workspace
    kUnopendFile   // Dependency file not currently opened
  };

  struct FileSpec {
    std::string content;
    FileRole role;
    std::string
        logical_name;  // For debugging/clarity (e.g., "module", "package")

    FileSpec(std::string content, FileRole role, std::string logical_name = "")
        : content(std::move(content)),
          role(role),
          logical_name(std::move(logical_name)) {
    }
  };

  // Result struct for role-based builds - bundles index with dependencies
  struct IndexWithRoles {
    std::unique_ptr<SemanticIndex> index;
    std::shared_ptr<slang::SourceManager> source_manager;
    std::unique_ptr<slang::ast::Compilation> compilation;
    std::vector<std::string> file_paths;
    std::string current_file_uri;  // The URI used for indexing
  };

  // Build index with explicit file roles for testing LSP scenarios
  auto static BuildIndexWithRoles(const std::vector<FileSpec>& files)
      -> IndexWithRoles {
    auto source_manager = std::make_shared<slang::SourceManager>();
    slang::Bag options;
    auto compilation = std::make_unique<slang::ast::Compilation>(options);

    std::vector<std::string> file_paths;
    std::string current_file_uri;
    int current_file_index = -1;

    // First pass: find the current file and assign indices
    for (size_t i = 0; i < files.size(); ++i) {
      if (files[i].role == FileRole::kCurrentFile) {
        if (current_file_index != -1) {
          throw std::runtime_error(
              "Multiple kCurrentFile roles specified - only one allowed");
        }
        current_file_index = static_cast<int>(i);
      }
    }

    if (current_file_index == -1) {
      throw std::runtime_error(
          "No kCurrentFile role specified - exactly one required");
    }

    // Second pass: add files to compilation in order, tracking the current
    // file's index
    for (size_t i = 0; i < files.size(); ++i) {
      const auto& file_spec = files[i];
      std::string filename = fmt::format("file_{}.sv", i);

      // Track which file becomes the indexing target
      if (static_cast<int>(i) == current_file_index) {
        current_file_uri = "file:///" + filename;
      }

      // Use consistent URI/path format
      std::string file_path = "/" + filename;
      file_paths.push_back(file_path);

      auto buffer = source_manager->assignText(file_path, file_spec.content);
      auto tree =
          slang::syntax::SyntaxTree::fromBuffer(buffer, *source_manager);
      compilation->addSyntaxTree(tree);
    }

    // Build index from the current file's perspective
    auto result = SemanticIndex::FromCompilation(
        *compilation, *source_manager, current_file_uri);

    if (!result) {
      throw std::runtime_error(
          fmt::format(
              "BuildIndexWithRoles: Failed to build semantic index: {}",
              result.error()));
    }

    auto index = std::move(*result);

    return IndexWithRoles{
        .index = std::move(index),
        .source_manager = std::move(source_manager),
        .compilation = std::move(compilation),
        .file_paths = std::move(file_paths),
        .current_file_uri = std::move(current_file_uri)};
  }

  // Build index from multiple files (improved version with file path tracking)
  auto static BuildIndexFromFilesWithPaths(
      const std::vector<std::string>& file_contents) -> IndexWithFiles {
    auto source_manager = std::make_shared<slang::SourceManager>();
    slang::Bag options;
    auto compilation = std::make_unique<slang::ast::Compilation>(options);

    std::vector<std::string> file_paths;

    // Add each file to the compilation
    for (size_t i = 0; i < file_contents.size(); ++i) {
      std::string filename = fmt::format("file_{}.sv", i);

      // Use consistent URI/path format
      std::string file_path = "/" + filename;

      file_paths.push_back(file_path);  // Track the actual file path created

      auto buffer = source_manager->assignText(file_path, file_contents[i]);
      auto tree =
          slang::syntax::SyntaxTree::fromBuffer(buffer, *source_manager);
      compilation->addSyntaxTree(tree);
    }

    // Use the first file URI for the index
    std::string first_file_uri = "file:///file_0.sv";
    auto result = SemanticIndex::FromCompilation(
        *compilation, *source_manager, first_file_uri);

    if (!result) {
      throw std::runtime_error(
          fmt::format(
              "BuildIndexFromFilesWithPaths: Failed to build semantic index: "
              "{}",
              result.error()));
    }

    auto index = std::move(*result);

    return IndexWithFiles{
        .index = std::move(index),
        .source_manager = std::move(source_manager),
        .compilation = std::move(compilation),
        .file_paths = std::move(file_paths),
        .uri = std::move(first_file_uri)};
  }

  // Build index from multiple files (simplified interface)
  auto static BuildIndexFromFiles(const std::vector<std::string>& file_contents)
      -> std::unique_ptr<SemanticIndex> {
    auto result = BuildIndexFromFilesWithPaths(file_contents);
    return std::move(result.index);
  }

  // Builder pattern for even clearer LSP scenario construction
  class IndexBuilder {
   public:
    explicit IndexBuilder() = default;

    auto SetCurrentFile(std::string content, std::string name = "current")
        -> IndexBuilder& {
      files_.emplace_back(
          std::move(content), FileRole::kCurrentFile, std::move(name));
      return *this;
    }

    auto AddUnopendFile(std::string content, std::string name = "dependency")
        -> IndexBuilder& {
      files_.emplace_back(
          std::move(content), FileRole::kUnopendFile, std::move(name));
      return *this;
    }

    auto AddOpenedFile(std::string content, std::string name = "opened")
        -> IndexBuilder& {
      files_.emplace_back(
          std::move(content), FileRole::kOpenedFile, std::move(name));
      return *this;
    }

    auto Build() -> IndexWithRoles {
      return BuildIndexWithRoles(files_);
    }

    auto BuildSimple() -> std::unique_ptr<SemanticIndex> {
      auto result = Build();
      return std::move(result.index);
    }

   private:
    std::vector<FileSpec> files_;
  };

  auto static CreateBuilder() -> IndexBuilder {
    return IndexBuilder();
  }

  // Helper to verify cross-file reference resolution
  static auto VerifySymbolReference(
      const SemanticIndex& index, const std::string& uri,
      const std::string& source, const std::string& symbol_name) -> bool {
    // Find the symbol usage location in source (returns LSP position)
    try {
      auto position = FindLocation(source, symbol_name);

      // Use LookupDefinitionAt API with provided URI
      auto def_range = index.LookupDefinitionAt(uri, position);
      return def_range.has_value();
    } catch (const std::runtime_error&) {
      return false;  // Symbol not found
    }
  }

  // Helper to check if cross-file references exist
  static auto HasCrossFileReferences(
      const SemanticIndex& index, const std::string& current_file_uri) -> bool {
    const auto& entries = index.GetSemanticEntries();
    return std::ranges::any_of(
        entries,
        [&current_file_uri](const slangd::semantic::SemanticEntry& entry) {
          // Cross-file if definition URI differs from current file URI
          return !entry.is_definition && entry.def_loc.uri != current_file_uri;
        });
  }

  // Build PreambleManager from temp directory files
  // Requires files to be written via CreateFile() first
  auto BuildPreambleManager(asio::any_io_executor executor) const
      -> asio::awaitable<std::shared_ptr<slangd::services::PreambleManager>> {
    auto layout_service = slangd::ProjectLayoutService::Create(
        executor, GetTempDir(), spdlog::default_logger());
    co_return co_await slangd::services::PreambleManager::
        CreateFromProjectLayout(
            layout_service, executor, spdlog::default_logger());
  }

  // Create BufferID offset package to force validation detection of missing
  // symbols Call this BEFORE creating test files when testing preamble symbol
  // coverage
  //
  // Why needed: Without BufferID offset, preamble BufferID 0 matches overlay
  // BufferID 0, causing missing symbol_info_ entries to produce
  // valid-but-wrong coordinates (false positive). With offset, preamble uses
  // BufferID 1+, causing conversion to produce invalid coordinates (line ==
  // -1), which validation catches.
  //
  // Use when: Testing new symbol types that might not be indexed properly
  auto CreateBufferIDOffset() -> void {
    const std::string offset_pkg = R"(
      package offset_pkg;
        parameter OFFSET = 1;
      endpackage
    )";
    CreateFile("offset_pkg.sv", offset_pkg);
  }

  // Build OverlaySession from disk files
  // Used for cross-file navigation tests with preamble support
  auto BuildSession(
      std::string_view current_file_name, asio::any_io_executor executor)
      -> asio::awaitable<std::shared_ptr<slangd::services::OverlaySession>> {
    auto layout_service = slangd::ProjectLayoutService::Create(
        executor, GetTempDir(), spdlog::default_logger());
    auto preamble_manager =
        co_await slangd::services::PreambleManager::CreateFromProjectLayout(
            layout_service, executor, spdlog::default_logger());

    // Read current file content from disk
    auto current_path = GetTempDir().Path() / current_file_name;
    std::ifstream file(current_path);
    std::string content(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());

    // Convert actual file path to URI
    std::string uri = CanonicalPath(current_path).ToUri();

    // Create OverlaySession with preamble_manager
    co_return slangd::services::OverlaySession::Create(
        uri, content, layout_service, preamble_manager);
  }

  static auto FindAllOccurrencesInSession(
      const slangd::services::OverlaySession& session,
      std::string_view symbol_name) -> std::vector<lsp::Location> {
    const auto& source_mgr = session.GetSourceManager();
    std::vector<lsp::Location> occurrences;

    for (slang::BufferID buffer : source_mgr.getAllBuffers()) {
      std::string_view buffer_content = source_mgr.getSourceText(buffer);
      std::string buffer_str(buffer_content);

      // Get URI from buffer using conversion utility
      slang::SourceLocation loc(buffer, 0);
      auto lsp_location = ToLspLocation(loc, source_mgr);
      std::string uri = lsp_location.uri;

      // Reuse base class helper to find all positions in this buffer
      auto positions = FindAllOccurrences(buffer_str, std::string(symbol_name));

      // Convert positions to locations with URI
      for (const auto& pos : positions) {
        // Calculate end position (symbol length)
        lsp::Position end_pos = pos;
        end_pos.character += static_cast<int>(symbol_name.length());

        occurrences.push_back(
            lsp::Location{
                .uri = uri, .range = lsp::Range{.start = pos, .end = end_pos}});
      }
    }

    return occurrences;
  }

  static auto FindLocationInSession(
      const slangd::services::OverlaySession& session,
      std::string_view symbol_name, size_t occurrence_index = 0)
      -> std::optional<lsp::Location> {
    auto occurrences = FindAllOccurrencesInSession(session, symbol_name);

    if (occurrence_index >= occurrences.size()) {
      return std::nullopt;
    }

    return occurrences[occurrence_index];
  }

  // High-level assertion helpers

  // Canonical assertion for cross-file definition navigation (LSP-first)
  // Verifies that go-to-definition from a symbol reference resolves correctly
  // to its definition in a different file
  //
  // Parameters:
  //   session: OverlaySession with PreambleManager for cross-file support
  //   ref_content: Source code containing the symbol reference
  //   def_content: Source code containing the symbol definition
  //   symbol: Symbol name to test (must exist in both contents)
  //   ref_index: Which occurrence in ref_content to use as reference (0-based)
  //   def_index: Which occurrence in def_content to expect as definition
  //   (0-based)
  //
  // Example:
  //   AssertCrossFileDef(*session, "import pkg::data_t;", "typedef logic
  //   data_t;",
  //                      "data_t", 0, 0);
  static void AssertCrossFileDef(
      const slangd::services::OverlaySession& session,
      std::string_view ref_content, std::string_view def_content,
      std::string_view symbol, size_t ref_index, size_t def_index) {
    // Find all occurrences in entire session (returns LSP locations)
    auto all_occurrences = FindAllOccurrencesInSession(session, symbol);

    // Reuse base class to find positions in source strings
    auto ref_positions =
        FindAllOccurrences(std::string(ref_content), std::string(symbol));
    auto def_positions =
        FindAllOccurrences(std::string(def_content), std::string(symbol));

    REQUIRE(ref_index < ref_positions.size());
    REQUIRE(def_index < def_positions.size());

    // Find the actual location in session that matches the reference position
    const lsp::Position& target_ref_pos = ref_positions[ref_index];
    auto ref_loc_it = std::ranges::find_if(
        all_occurrences,
        [&](const auto& loc) { return loc.range.start == target_ref_pos; });
    REQUIRE(ref_loc_it != all_occurrences.end());

    // Lookup definition using LSP coordinates
    auto def_loc = session.GetSemanticIndex().LookupDefinitionAt(
        ref_loc_it->uri, ref_loc_it->range.start);
    REQUIRE(def_loc.has_value());

    // Verify cross-file (different URIs)
    REQUIRE(def_loc->uri != ref_loc_it->uri);

    // Verify definition position matches expected
    const lsp::Position& target_def_pos = def_positions[def_index];
    REQUIRE(def_loc->range.start.line == target_def_pos.line);
    REQUIRE(def_loc->range.start.character == target_def_pos.character);

    // Verify range length
    auto range_length =
        def_loc->range.end.character - def_loc->range.start.character;
    REQUIRE(range_length == static_cast<int>(symbol.length()));
  }

  static void AssertSameFileDefinition(
      const SemanticIndex& index, const std::string& uri,
      const std::string& code, const std::string& symbol,
      size_t reference_index = 0) {
    // Pure LSP: find all occurrences as positions
    auto occurrences = FindAllOccurrences(code, symbol);

    if (reference_index >= occurrences.size()) {
      throw std::runtime_error(
          fmt::format(
              "AssertSameFileDefinition: reference_index {} out of range for "
              "symbol '{}' (found {} occurrences)",
              reference_index, symbol, occurrences.size()));
    }

    auto position = occurrences[reference_index];

    // Lookup using LSP coordinates
    auto def_loc = index.LookupDefinitionAt(uri, position);
    REQUIRE(def_loc.has_value());

    // For same-file references, verify URI matches
    REQUIRE(def_loc->uri == uri);

    // Verify range length
    auto range_length =
        def_loc->range.end.character - def_loc->range.start.character;
    REQUIRE(range_length == static_cast<int>(symbol.length()));
  }

  static void AssertDefinitionNotCrash(
      const SemanticIndex& index, const std::string& uri,
      const std::string& code, const std::string& symbol) {
    // Pure LSP: FindLocation returns lsp::Position directly
    auto position = FindLocation(code, symbol);

    // Just check it doesn't crash - ignore return value
    (void)index.LookupDefinitionAt(uri, position);
  }

  // Diagnostic assertion helpers
  // These verify that compilation succeeded without errors/warnings

  // Assert that compilation has no parse or semantic diagnostics
  // This triggers full elaboration to catch all errors and warnings
  static void AssertNoDiagnostics(
      const slangd::services::OverlaySession& session) {
    auto all_diags = GetDiagnostics(session);

    if (!all_diags.empty()) {
      std::string error_msg = fmt::format(
          "Expected no diagnostics, but found {}:\n", all_diags.size());
      for (const auto& diag : all_diags) {
        error_msg += fmt::format(
            "  [{}] Line {}: {}\n", diag.code.value_or("unknown"),
            diag.range.start.line, diag.message);
      }
      FAIL(error_msg);
    }
  }

  // Assert that compilation has no errors (warnings are allowed)
  // This triggers full elaboration to catch all errors
  static void AssertNoErrors(const slangd::services::OverlaySession& session) {
    auto all_diags = GetDiagnostics(session);

    std::vector<lsp::Diagnostic> errors;
    for (const auto& diag : all_diags) {
      if (diag.severity == lsp::DiagnosticSeverity::kError) {
        errors.push_back(diag);
      }
    }

    if (!errors.empty()) {
      std::string error_msg =
          fmt::format("Expected no errors, but found {}:\n", errors.size());
      for (const auto& diag : errors) {
        error_msg += fmt::format(
            "  [{}] Line {}: {}\n", diag.code.value_or("unknown"),
            diag.range.start.line, diag.message);
      }
      FAIL(error_msg);
    }
  }

  // Get diagnostics collected during semantic indexing
  // Uses safe API that doesn't trigger full elaboration
  static auto GetDiagnostics(const slangd::services::OverlaySession& session)
      -> std::vector<lsp::Diagnostic> {
    auto& compilation = session.GetCompilation();
    const auto& source_manager = session.GetSourceManager();
    auto main_buffer_id = session.GetMainBufferID();

    // SAFE API: getCollectedDiagnostics() reads diagMap without triggering
    // elaboration diagMap was already populated during
    // SemanticIndex::FromCompilation()
    const auto& slang_diags = compilation.getCollectedDiagnostics();
    return slangd::semantic::DiagnosticConverter::ExtractDiagnostics(
        slang_diags, source_manager, main_buffer_id);
  }
};

}  // namespace slangd::test

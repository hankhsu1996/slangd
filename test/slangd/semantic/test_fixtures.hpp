#pragma once

#include <algorithm>
#include <fstream>
#include <memory>
#include <regex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <asio.hpp>
#include <catch2/catch_all.hpp>
#include <fmt/format.h>
#include <slang/ast/Compilation.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/text/SourceLocation.h>
#include <slang/text/SourceManager.h>
#include <slang/util/Bag.h>
#include <spdlog/spdlog.h>

#include "slangd/core/project_layout_service.hpp"
#include "slangd/semantic/semantic_index.hpp"
#include "slangd/services/overlay_session.hpp"
#include "slangd/services/preamble_manager.hpp"
#include "test/slangd/common/file_fixture.hpp"

namespace slangd::semantic::test {

// Base fixture for all semantic index tests
class SemanticTestFixture {
 public:
  using SemanticIndex = slangd::semantic::SemanticIndex;
  using SymbolKey = slangd::semantic::SymbolKey;
  auto BuildIndexFromSource(const std::string& source)
      -> std::unique_ptr<SemanticIndex> {
    constexpr std::string_view kTestFilename = "test.sv";

    // Use consistent URI/path format
    std::string test_uri = "file:///" + std::string(kTestFilename);
    std::string test_path = "/" + std::string(kTestFilename);

    SetSourceManager(std::make_shared<slang::SourceManager>());
    auto buffer = GetSourceManager()->assignText(test_path, source);
    SetBufferId(buffer.id);
    auto tree =
        slang::syntax::SyntaxTree::fromBuffer(buffer, *GetSourceManager());

    slang::Bag options;
    SetCompilation(std::make_unique<slang::ast::Compilation>(options));
    GetCompilation()->addSyntaxTree(tree);

    return SemanticIndex::FromCompilation(
        *GetCompilation(), *GetSourceManager(), test_uri);
  }

  auto MakeKey(const std::string& source, const std::string& symbol)
      -> SymbolKey {
    size_t offset = source.find(symbol);

    if (offset == std::string::npos) {
      throw std::runtime_error(
          fmt::format("MakeKey: Symbol '{}' not found in source", symbol));
    }

    // Detect ambiguous symbol names early
    size_t second_occurrence = source.find(symbol, offset + 1);
    if (second_occurrence != std::string::npos) {
      throw std::runtime_error(
          fmt::format(
              "MakeKey: Ambiguous symbol '{}' found at multiple locations. "
              "Use unique descriptive names (e.g., 'test_signal' not 'signal') "
              "or use MakeKeyAt({}) for specific occurrence.",
              symbol, offset));
    }

    return SymbolKey{.bufferId = buffer_id_.getId(), .offset = offset};
  }

  // Alternative method for cases where multiple occurrences are expected
  auto MakeKeyAt(
      const std::string& source, const std::string& symbol,
      size_t occurrence = 0) -> SymbolKey {
    size_t offset = 0;
    for (size_t i = 0; i <= occurrence; ++i) {
      offset = source.find(symbol, offset);
      if (offset == std::string::npos) {
        throw std::runtime_error(
            fmt::format(
                "MakeKeyAt: Symbol '{}' occurrence {} not found in source",
                symbol, occurrence));
      }
      if (i < occurrence) {
        offset += symbol.length();
      }
    }
    return SymbolKey{.bufferId = buffer_id_.getId(), .offset = offset};
  }

  auto MakeRange(
      const std::string& source, const std::string& search_string,
      size_t symbol_size) -> slang::SourceRange {
    size_t offset = source.find(search_string);
    auto start = slang::SourceLocation{buffer_id_, offset};
    auto end = slang::SourceLocation{buffer_id_, offset + symbol_size};
    return slang::SourceRange{start, end};
  }

  auto FindLocation(const std::string& source, const std::string& text)
      -> slang::SourceLocation {
    size_t offset = source.find(text);
    if (offset == std::string::npos) {
      return {};
    }
    return slang::SourceLocation{buffer_id_, offset};
  }

  static auto FindSymbolOffsetsInText(
      const std::string& text, std::string_view symbol_name)
      -> std::vector<size_t> {
    std::vector<size_t> offsets;
    std::string pattern = R"((?:^|[\s.]))" + std::string(symbol_name) + R"(\b)";
    std::regex symbol_regex(pattern);

    auto begin = std::sregex_iterator(text.begin(), text.end(), symbol_regex);
    auto end = std::sregex_iterator();

    for (auto it = begin; it != end; ++it) {
      auto match_pos = static_cast<size_t>(it->position());
      if (match_pos < text.size() && (std::isalnum(text[match_pos]) == 0) &&
          text[match_pos] != '_') {
        match_pos += 1;
      }
      offsets.push_back(match_pos);
    }

    return offsets;
  }

  auto FindAllOccurrences(
      const std::string& code, const std::string& symbol_name)
      -> std::vector<slang::SourceLocation> {
    auto offsets = FindSymbolOffsetsInText(code, symbol_name);

    if (offsets.empty()) {
      throw std::runtime_error(
          fmt::format(
              "FindAllOccurrences: No occurrences of '{}' found", symbol_name));
    }

    std::vector<slang::SourceLocation> occurrences;
    for (size_t offset : offsets) {
      occurrences.emplace_back(buffer_id_, offset);
    }

    return occurrences;
  }

  // Public accessors for derived classes and tests
  [[nodiscard]] auto GetBufferId() const -> uint32_t {
    return buffer_id_.getId();
  }
  [[nodiscard]] auto GetSourceManager() const -> slang::SourceManager* {
    return source_manager_.get();
  }
  [[nodiscard]] auto GetCompilation() const -> slang::ast::Compilation* {
    return compilation_.get();
  }

 protected:
  // Protected setters for derived classes to modify state
  void SetSourceManager(std::shared_ptr<slang::SourceManager> sm) {
    source_manager_ = std::move(sm);
  }
  void SetCompilation(std::unique_ptr<slang::ast::Compilation> comp) {
    compilation_ = std::move(comp);
  }
  void SetBufferId(slang::BufferID id) {
    buffer_id_ = id;
  }

 private:
  std::shared_ptr<slang::SourceManager> source_manager_;
  std::unique_ptr<slang::ast::Compilation> compilation_;
  slang::BufferID buffer_id_;
};

// Extended fixture for multifile tests
class MultiFileSemanticFixture : public SemanticTestFixture,
                                 public slangd::test::FileTestFixture {
 public:
  MultiFileSemanticFixture()
      : slangd::test::FileTestFixture("slangd_semantic_multifile") {
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

  // Result struct for BuildIndexFromFiles - includes both index and file paths
  struct IndexWithFiles {
    std::unique_ptr<SemanticIndex> index;
    std::vector<std::string> file_paths;  // The actual file paths created
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

  // Result struct for role-based builds
  struct IndexWithRoles {
    std::unique_ptr<SemanticIndex> index;
    std::vector<std::string> file_paths;
    std::string current_file_uri;  // The URI used for indexing
  };

  // Build index with explicit file roles for testing LSP scenarios
  auto BuildIndexWithRoles(const std::vector<FileSpec>& files)
      -> IndexWithRoles {
    SetSourceManager(std::make_shared<slang::SourceManager>());
    slang::Bag options;
    SetCompilation(std::make_unique<slang::ast::Compilation>(options));

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

      auto buffer =
          GetSourceManager()->assignText(file_path, file_spec.content);
      auto tree =
          slang::syntax::SyntaxTree::fromBuffer(buffer, *GetSourceManager());
      GetCompilation()->addSyntaxTree(tree);

      // Store the first buffer ID for key creation compatibility
      if (i == 0) {
        SetBufferId(buffer.id);
      }
    }

    // Build index from the current file's perspective
    auto index = SemanticIndex::FromCompilation(
        *GetCompilation(), *GetSourceManager(), current_file_uri);

    return IndexWithRoles{
        .index = std::move(index),
        .file_paths = std::move(file_paths),
        .current_file_uri = current_file_uri};
  }

  // Build index from multiple files (improved version with file path tracking)
  auto BuildIndexFromFilesWithPaths(
      const std::vector<std::string>& file_contents) -> IndexWithFiles {
    SetSourceManager(std::make_shared<slang::SourceManager>());
    slang::Bag options;
    SetCompilation(std::make_unique<slang::ast::Compilation>(options));

    std::vector<std::string> file_paths;

    // Add each file to the compilation
    for (size_t i = 0; i < file_contents.size(); ++i) {
      std::string filename = fmt::format("file_{}.sv", i);

      // Use consistent URI/path format
      std::string file_uri = "file:///" + filename;
      std::string file_path = "/" + filename;

      file_paths.push_back(file_path);  // Track the actual file path created

      auto buffer = GetSourceManager()->assignText(file_path, file_contents[i]);
      auto tree =
          slang::syntax::SyntaxTree::fromBuffer(buffer, *GetSourceManager());
      GetCompilation()->addSyntaxTree(tree);

      // Store the first buffer ID for key creation
      if (i == 0) {
        SetBufferId(buffer.id);
      }
    }

    // Use the first file URI for the index
    std::string first_file_uri = "file:///file_0.sv";
    auto index = SemanticIndex::FromCompilation(
        *GetCompilation(), *GetSourceManager(), first_file_uri);

    return IndexWithFiles{
        .index = std::move(index), .file_paths = std::move(file_paths)};
  }

  // Build index from multiple files (simplified interface)
  auto BuildIndexFromFiles(const std::vector<std::string>& file_contents)
      -> std::unique_ptr<SemanticIndex> {
    auto result = BuildIndexFromFilesWithPaths(file_contents);
    return std::move(result.index);
  }

  // Builder pattern for even clearer LSP scenario construction
  class IndexBuilder {
   public:
    explicit IndexBuilder(MultiFileSemanticFixture* fixture)
        : fixture_(fixture) {
    }

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
      return fixture_->BuildIndexWithRoles(files_);
    }

    auto BuildSimple() -> std::unique_ptr<SemanticIndex> {
      auto result = Build();
      return std::move(result.index);
    }

   private:
    MultiFileSemanticFixture* fixture_;
    std::vector<FileSpec> files_;
  };

  auto CreateBuilder() -> IndexBuilder {
    return IndexBuilder(this);
  }

  // Helper to verify cross-file reference resolution
  auto VerifySymbolReference(
      const SemanticIndex& index, const std::string& source,
      const std::string& symbol_name) -> bool {
    // Find the symbol usage location in source
    auto location = FindLocation(source, symbol_name);
    if (!location.valid()) {
      return false;
    }

    // Use LookupDefinitionAt API
    auto def_range = index.LookupDefinitionAt(location);
    return def_range.has_value();
  }

  // Helper to check if cross-file references exist
  static auto HasCrossFileReferences(const SemanticIndex& index) -> bool {
    const auto& entries = index.GetSemanticEntries();
    return std::ranges::any_of(entries, [](const SemanticEntry& entry) {
      // Check if source and definition are in different buffers
      return !entry.is_definition &&
             entry.source_range.start().buffer().getId() !=
                 entry.definition_range.start().buffer().getId();
    });
  }

  // Build PreambleManager from temp directory files
  // Requires files to be written via CreateFile() first
  auto BuildPreambleManager(asio::any_io_executor executor) const
      -> std::shared_ptr<slangd::services::PreambleManager> {
    auto layout_service = slangd::ProjectLayoutService::Create(
        executor, GetTempDir(), spdlog::default_logger());
    return slangd::services::PreambleManager::CreateFromProjectLayout(
        layout_service, spdlog::default_logger());
  }

  struct SessionWithPreambleManager {
    std::shared_ptr<slangd::services::OverlaySession> session;
    std::shared_ptr<slangd::services::PreambleManager> preamble_manager;
  };

  // Build OverlaySession from disk files with PreambleManager
  // Used for cross-file navigation tests
  // Returns both session and preamble_manager for test access
  auto BuildSessionWithPreamble(
      std::string_view current_file_name, asio::any_io_executor executor)
      -> SessionWithPreambleManager {
    auto layout_service = slangd::ProjectLayoutService::Create(
        executor, GetTempDir(), spdlog::default_logger());
    auto preamble_manager =
        slangd::services::PreambleManager::CreateFromProjectLayout(
            layout_service, spdlog::default_logger());

    // Read current file content from disk
    auto current_path = GetTempDir().Path() / current_file_name;
    std::ifstream file(current_path);
    std::string content(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());

    std::string uri = "file:///" + std::string(current_file_name);

    // Create OverlaySession with preamble_manager (handles all compilation
    // setup)
    auto session = slangd::services::OverlaySession::Create(
        uri, content, layout_service, preamble_manager);

    return {.session = session, .preamble_manager = preamble_manager};
  }

  static auto FindAllOccurrencesInSession(
      const slangd::services::OverlaySession& session,
      std::string_view symbol_name) -> std::vector<slang::SourceLocation> {
    const auto& source_mgr = session.GetSourceManager();
    std::vector<slang::SourceLocation> occurrences;

    for (slang::BufferID buffer : source_mgr.getAllBuffers()) {
      std::string_view buffer_content = source_mgr.getSourceText(buffer);
      std::string buffer_str(buffer_content);

      auto offsets = FindSymbolOffsetsInText(buffer_str, symbol_name);

      for (size_t offset : offsets) {
        occurrences.emplace_back(buffer, offset);
      }
    }

    return occurrences;
  }

  static auto FindLocationInSession(
      const slangd::services::OverlaySession& session,
      std::string_view symbol_name, size_t occurrence_index = 0)
      -> slang::SourceLocation {
    auto occurrences = FindAllOccurrencesInSession(session, symbol_name);

    if (occurrence_index >= occurrences.size()) {
      return {};
    }

    return occurrences[occurrence_index];
  }

  // High-level assertion helpers

  void AssertCrossFileDefinition(
      const SemanticIndex& index, const std::string& code,
      const std::string& symbol) {
    auto location = FindLocation(code, symbol);
    REQUIRE(location.valid());

    auto def_loc = index.LookupDefinitionAt(location);
    REQUIRE(def_loc.has_value());

    // For package imports, they're in overlay so should be same_file_range
    // For modules, they should be cross_file_path
    bool is_cross_file =
        def_loc->cross_file_path.has_value() ||
        (def_loc->same_file_range.has_value() &&
         def_loc->same_file_range->start().buffer() != location.buffer());
    REQUIRE(is_cross_file);
  }

  static void AssertCrossFileDefinition(
      const SessionWithPreambleManager& result, std::string_view symbol,
      std::string_view expected_source_file,
      std::string_view expected_def_file) {
    auto location = FindLocationInSession(*result.session, symbol);
    REQUIRE(location.valid());

    auto def_loc =
        result.session->GetSemanticIndex().LookupDefinitionAt(location);
    REQUIRE(def_loc.has_value());

    auto location_file =
        result.session->GetSourceManager().getFileName(location);
    REQUIRE(location_file.find(expected_source_file) != std::string_view::npos);

    REQUIRE(def_loc->cross_file_path.has_value());
    REQUIRE(def_loc->cross_file_range.has_value());

    auto def_file = def_loc->cross_file_path->String();
    REQUIRE(def_file.find(expected_def_file) != std::string_view::npos);

    REQUIRE(def_loc->cross_file_range->start.line >= 0);
    REQUIRE(def_loc->cross_file_range->start.character >= 0);
    REQUIRE(def_loc->cross_file_range->end.line >= 0);
    REQUIRE(def_loc->cross_file_range->end.character >= 0);

    auto range_length = def_loc->cross_file_range->end.character -
                        def_loc->cross_file_range->start.character;
    REQUIRE(range_length == static_cast<int>(symbol.length()));
  }

  static void AssertCrossFileDef(
      const SessionWithPreambleManager& result, std::string_view ref_content,
      std::string_view def_content, std::string_view symbol, size_t ref_index,
      size_t def_index) {
    auto ref_offsets =
        FindSymbolOffsetsInText(std::string(ref_content), symbol);
    REQUIRE(ref_index < ref_offsets.size());

    auto def_offsets =
        FindSymbolOffsetsInText(std::string(def_content), symbol);
    REQUIRE(def_index < def_offsets.size());

    auto ref_location =
        FindLocationInSession(*result.session, symbol, ref_index);
    REQUIRE(ref_location.valid());

    auto def_loc =
        result.session->GetSemanticIndex().LookupDefinitionAt(ref_location);
    REQUIRE(def_loc.has_value());
    REQUIRE(def_loc->cross_file_path.has_value());
    REQUIRE(def_loc->cross_file_range.has_value());

    auto range_length = def_loc->cross_file_range->end.character -
                        def_loc->cross_file_range->start.character;
    REQUIRE(range_length == static_cast<int>(symbol.length()));
  }

  static void AssertCrossFileDefinitionAt(
      const SessionWithPreambleManager& result, std::string_view symbol,
      size_t occurrence_index, std::string_view expected_source_file,
      std::string_view expected_def_file) {
    auto occurrences = FindAllOccurrencesInSession(*result.session, symbol);
    REQUIRE(occurrence_index < occurrences.size());

    auto location = occurrences[occurrence_index];
    REQUIRE(location.valid());

    auto def_loc =
        result.session->GetSemanticIndex().LookupDefinitionAt(location);
    REQUIRE(def_loc.has_value());

    auto location_file =
        result.session->GetSourceManager().getFileName(location);
    REQUIRE(location_file.find(expected_source_file) != std::string_view::npos);

    REQUIRE(def_loc->cross_file_path.has_value());
    REQUIRE(def_loc->cross_file_range.has_value());

    auto def_file = def_loc->cross_file_path->String();
    REQUIRE(def_file.find(expected_def_file) != std::string_view::npos);

    REQUIRE(def_loc->cross_file_range->start.line >= 0);
    REQUIRE(def_loc->cross_file_range->start.character >= 0);
    REQUIRE(def_loc->cross_file_range->end.line >= 0);
    REQUIRE(def_loc->cross_file_range->end.character >= 0);

    auto range_length = def_loc->cross_file_range->end.character -
                        def_loc->cross_file_range->start.character;
    REQUIRE(range_length == static_cast<int>(symbol.length()));
  }

  void AssertSameFileDefinition(
      const SemanticIndex& index, const std::string& code,
      const std::string& symbol, size_t reference_index = 0) {
    auto occurrences = FindAllOccurrences(code, symbol);

    if (reference_index >= occurrences.size()) {
      throw std::runtime_error(
          fmt::format(
              "AssertSameFileDefinition: reference_index {} out of range for "
              "symbol '{}' (found {} occurrences)",
              reference_index, symbol, occurrences.size()));
    }

    auto location = occurrences[reference_index];
    REQUIRE(location.valid());

    auto def_loc = index.LookupDefinitionAt(location);
    REQUIRE(def_loc.has_value());

    REQUIRE(!def_loc->cross_file_path.has_value());
    REQUIRE(def_loc->same_file_range.has_value());

    auto actual_start = def_loc->same_file_range->start().offset();
    auto actual_end = def_loc->same_file_range->end().offset();
    auto expected_def_offset = occurrences[0].offset();

    REQUIRE(actual_start == expected_def_offset);
    REQUIRE(actual_end - actual_start == symbol.length());
  }

  void AssertDefinitionNotCrash(
      const SemanticIndex& index, const std::string& code,
      const std::string& symbol) {
    auto location = FindLocation(code, symbol);
    REQUIRE(location.valid());

    // Just check it doesn't crash - ignore return value
    (void)index.LookupDefinitionAt(location);
  }
};

}  // namespace slangd::semantic::test

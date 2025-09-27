#include "simple_fixture.hpp"

#include <regex>
#include <stdexcept>

#include <fmt/format.h>
#include <slang/ast/Compilation.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/text/SourceManager.h>
#include <slang/util/Bag.h>

namespace slangd::test {

auto SimpleTestFixture::CompileSource(const std::string& code)
    -> std::unique_ptr<semantic::SemanticIndex> {
  constexpr std::string_view kTestFilename = "test.sv";

  // Use consistent URI/path format
  std::string test_uri = "file:///" + std::string(kTestFilename);
  std::string test_path = "/" + std::string(kTestFilename);

  source_manager_ = std::make_shared<slang::SourceManager>();
  auto buffer = source_manager_->assignText(test_path, code);
  buffer_id_ = buffer.id;
  auto tree = slang::syntax::SyntaxTree::fromBuffer(buffer, *source_manager_);

  slang::Bag options;
  // Enable LSP mode to activate expression preservation for typedef parameter
  // references
  options.set<slang::ast::CompilationFlags>(
      slang::ast::CompilationFlags::LanguageServerMode);
  compilation_ = std::make_unique<slang::ast::Compilation>(options);
  compilation_->addSyntaxTree(tree);

  auto index = semantic::SemanticIndex::FromCompilation(
      *compilation_, *source_manager_, test_uri);

  // Validate that compilation succeeded
  if (!index) {
    throw std::runtime_error("CompileSource: Failed to create semantic index");
  }

  return index;
}

auto SimpleTestFixture::FindSymbol(
    const std::string& code, const std::string& name) -> slang::SourceLocation {
  size_t offset = code.find(name);
  if (offset == std::string::npos) {
    throw std::runtime_error(
        fmt::format("FindSymbol: Symbol '{}' not found in source", name));
  }

  // Detect ambiguous symbol names early
  size_t second_occurrence = code.find(name, offset + 1);
  if (second_occurrence != std::string::npos) {
    throw std::runtime_error(
        fmt::format(
            "FindSymbol: Ambiguous symbol '{}' found at multiple locations. "
            "Use unique descriptive names in test code.",
            name));
  }

  return slang::SourceLocation{buffer_id_, offset};
}

auto SimpleTestFixture::GetDefinitionRange(
    semantic::SemanticIndex& index, slang::SourceLocation loc)
    -> std::optional<slang::SourceRange> {
  return index.LookupDefinitionAt(loc);
}

auto SimpleTestFixture::FindAllOccurrences(
    const std::string& code, const std::string& symbol_name)
    -> std::vector<slang::SourceLocation> {
  std::vector<slang::SourceLocation> occurrences;

  // Create regex pattern for complete identifier match
  // \b = word boundary, ensures we match complete identifiers only
  std::string pattern = R"(\b)" + symbol_name + R"(\b)";
  std::regex symbol_regex(pattern);

  // Use sregex_iterator for elegant iteration over all matches
  auto begin = std::sregex_iterator(code.begin(), code.end(), symbol_regex);
  auto end = std::sregex_iterator();

  for (auto it = begin; it != end; ++it) {
    const std::smatch& match = *it;
    occurrences.emplace_back(buffer_id_, match.position());
  }

  if (occurrences.empty()) {
    throw std::runtime_error(
        fmt::format(
            "FindAllOccurrences: No occurrences of '{}' found", symbol_name));
  }

  return occurrences;
}

void SimpleTestFixture::AssertGoToDefinition(
    semantic::SemanticIndex& index, const std::string& code,
    const std::string& symbol_name, size_t reference_index,
    size_t definition_index) {
  auto occurrences = FindAllOccurrences(code, symbol_name);

  if (reference_index >= occurrences.size()) {
    throw std::runtime_error(
        fmt::format(
            "AssertGoToDefinition: reference_index {} out of range for symbol "
            "'{}' (found {} occurrences)",
            reference_index, symbol_name, occurrences.size()));
  }

  if (definition_index >= occurrences.size()) {
    throw std::runtime_error(
        fmt::format(
            "AssertGoToDefinition: definition_index {} out of range for symbol "
            "'{}' (found {} occurrences)",
            definition_index, symbol_name, occurrences.size()));
  }

  auto reference_loc = occurrences[reference_index];
  auto expected_def_loc = occurrences[definition_index];

  // Perform go-to-definition lookup
  auto actual_def_range = index.LookupDefinitionAt(reference_loc);

  if (!actual_def_range.has_value()) {
    throw std::runtime_error(
        fmt::format(
            "AssertGoToDefinition: LookupDefinitionAt failed for symbol '{}' "
            "at reference_index {}",
            symbol_name, reference_index));
  }

  if (!actual_def_range->contains(expected_def_loc)) {
    throw std::runtime_error(
        fmt::format(
            "AssertGoToDefinition: definition range does not contain expected "
            "location for symbol '{}'",
            symbol_name));
  }
}

void SimpleTestFixture::AssertReferenceExists(
    semantic::SemanticIndex& index, const std::string& code,
    const std::string& symbol_name, size_t reference_index) {
  auto occurrences = FindAllOccurrences(code, symbol_name);

  if (reference_index >= occurrences.size()) {
    throw std::runtime_error(
        fmt::format(
            "AssertReferenceExists: reference_index {} out of range for symbol "
            "'{}' (found {} occurrences)",
            reference_index, symbol_name, occurrences.size()));
  }

  auto reference_loc = occurrences[reference_index];

  // Check that the reference location produces a valid go-to-definition result
  auto def_range = index.LookupDefinitionAt(reference_loc);

  if (!def_range.has_value()) {
    throw std::runtime_error(
        fmt::format(
            "AssertReferenceExists: reference not found for symbol '{}' at "
            "reference_index {}",
            symbol_name, reference_index));
  }
}

void SimpleTestFixture::AssertHasSymbols(semantic::SemanticIndex& index) {
  if (index.GetSymbolCount() == 0) {
    throw std::runtime_error(
        "AssertHasSymbols: Expected symbols but index is empty");
  }
}

void SimpleTestFixture::AssertSymbolAtLocation(
    semantic::SemanticIndex& index, const std::string& code,
    const std::string& symbol_name, lsp::SymbolKind expected_kind) {
  // Find symbol location (this makes the two-step process explicit)
  auto location = FindSymbol(code, symbol_name);
  if (!location.valid()) {
    throw std::runtime_error(
        fmt::format(
            "AssertSymbolAtLocation: Invalid location for symbol '{}'",
            symbol_name));
  }

  // Perform O(1) lookup
  auto symbol_info = index.GetSymbolAt(location);
  if (!symbol_info.has_value()) {
    throw std::runtime_error(
        fmt::format(
            "AssertSymbolAtLocation: No symbol found at location for '{}'",
            symbol_name));
  }

  // Verify symbol properties
  if (std::string(symbol_info->symbol->name) != symbol_name) {
    throw std::runtime_error(
        fmt::format(
            "AssertSymbolAtLocation: Expected symbol name '{}' but got '{}'",
            symbol_name, std::string(symbol_info->symbol->name)));
  }

  if (symbol_info->lsp_kind != expected_kind) {
    throw std::runtime_error(
        fmt::format(
            "AssertSymbolAtLocation: Expected symbol kind but got different "
            "kind for '{}'",
            symbol_name));
  }
}

}  // namespace slangd::test

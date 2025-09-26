#pragma once
#include <memory>
#include <optional>
#include <string>

#include <slang/text/SourceLocation.h>

#include "slangd/semantic/semantic_index.hpp"

namespace slangd::test {

class SimpleTestFixture {
 public:
  // Compile source and return semantic index
  auto CompileSource(const std::string& code)
      -> std::unique_ptr<semantic::SemanticIndex>;

  // Find symbol location in source by name (must be unique)
  auto FindSymbol(const std::string& code, const std::string& name)
      -> slang::SourceLocation;

  // Get definition range for symbol at location
  static auto GetDefinitionRange(
      semantic::SemanticIndex* index, slang::SourceLocation loc)
      -> std::optional<slang::SourceRange>;

  // High-level API for clean go-to-definition testing

  // Find all occurrences of a symbol in source code (ordered by appearance)
  auto FindAllOccurrences(
      const std::string& code, const std::string& symbol_name)
      -> std::vector<slang::SourceLocation>;

  // Assert that go-to-definition works: reference at ref_index points to
  // definition at def_index
  void AssertGoToDefinition(
      semantic::SemanticIndex* index, const std::string& code,
      const std::string& symbol_name, size_t reference_index,
      size_t definition_index);

  // Assert that a reference was captured by the semantic index
  void AssertReferenceExists(
      semantic::SemanticIndex* index, const std::string& code,
      const std::string& symbol_name, size_t reference_index);

 private:
  std::shared_ptr<slang::SourceManager> source_manager_;
  std::unique_ptr<slang::ast::Compilation> compilation_;
  slang::BufferID buffer_id_;
};

}  // namespace slangd::test

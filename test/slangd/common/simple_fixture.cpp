#include "simple_fixture.hpp"

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
  compilation_ = std::make_unique<slang::ast::Compilation>(options);
  compilation_->addSyntaxTree(tree);

  return semantic::SemanticIndex::FromCompilation(
      *compilation_, *source_manager_, test_uri);
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
    throw std::runtime_error(fmt::format(
        "FindSymbol: Ambiguous symbol '{}' found at multiple locations. "
        "Use unique descriptive names in test code.",
        name));
  }

  return slang::SourceLocation{buffer_id_, offset};
}

auto SimpleTestFixture::GetDefinitionRange(
    semantic::SemanticIndex* index, slang::SourceLocation loc)
    -> std::optional<slang::SourceRange> {
  return index->LookupDefinitionAt(loc);
}

}  // namespace slangd::test

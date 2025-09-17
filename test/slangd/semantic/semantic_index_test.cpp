#include "slangd/semantic/semantic_index.hpp"

#include <memory>
#include <string>

#include <catch2/catch_all.hpp>
#include <slang/ast/Compilation.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/text/SourceManager.h>
#include <slang/util/Bag.h>

auto main(int argc, char* argv[]) -> int {
  return Catch::Session().run(argc, argv);
}

namespace slangd::semantic {

TEST_CASE(
    "SemanticIndex processes symbols via preVisit hook", "[semantic_index]") {
  std::string code = R"(
    module test_module;
      logic signal;
    endmodule
  )";

  auto source_manager = std::make_shared<slang::SourceManager>();
  slang::Bag options;
  auto compilation = std::make_unique<slang::ast::Compilation>(options);

  auto buffer = source_manager->assignText("test.sv", code);
  auto tree = slang::syntax::SyntaxTree::fromBuffer(buffer, *source_manager);
  if (tree) {
    compilation->addSyntaxTree(tree);
  }

  auto index = SemanticIndex::FromCompilation(*compilation);

  REQUIRE(index != nullptr);
  // The preVisit hook should collect symbols from the root traversal
  REQUIRE(index->GetSymbolCount() > 0);
}

}  // namespace slangd::semantic

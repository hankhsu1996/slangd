#pragma once

#include <memory>
#include <unordered_map>

#include <slang/ast/Compilation.h>
#include <slang/ast/Symbol.h>
#include <slang/text/SourceLocation.h>

namespace slangd::semantic {

// SemanticIndex replaces separate DefinitionIndex and SymbolIndex with a single
// system that processes ALL symbol types for complete LSP coverage
class SemanticIndex {
 public:
  struct SymbolInfo {
    const slang::ast::Symbol* symbol{};
    slang::SourceLocation location;
  };

  static auto FromCompilation(slang::ast::Compilation& compilation)
      -> std::unique_ptr<SemanticIndex>;

  auto GetSymbolCount() const -> size_t {
    return symbols_.size();
  }

 private:
  explicit SemanticIndex() = default;

  std::unordered_map<slang::SourceLocation, SymbolInfo> symbols_;
};

}  // namespace slangd::semantic

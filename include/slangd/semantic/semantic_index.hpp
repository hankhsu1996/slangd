#pragma once

#include <memory>
#include <optional>
#include <unordered_map>

#include <lsp/document_features.hpp>
#include <slang/ast/ASTVisitor.h>
#include <slang/ast/Compilation.h>
#include <slang/ast/Symbol.h>
#include <slang/text/SourceLocation.h>
#include <slang/text/SourceManager.h>

namespace slangd::semantic {

// SemanticIndex replaces separate DefinitionIndex and SymbolIndex with a single
// system that processes ALL symbol types for complete LSP coverage
class SemanticIndex {
 public:
  struct SymbolInfo {
    const slang::ast::Symbol* symbol{};
    slang::SourceLocation location;
    lsp::SymbolKind lsp_kind{};
    lsp::Range range{};
    const slang::ast::Scope* parent{};
  };

  static auto FromCompilation(
      slang::ast::Compilation& compilation,
      const slang::SourceManager& source_manager)
      -> std::unique_ptr<SemanticIndex>;

  // Query methods
  auto GetSymbolCount() const -> size_t {
    return symbols_.size();
  }

  auto GetSymbolAt(slang::SourceLocation location) const
      -> std::optional<SymbolInfo>;

  auto GetAllSymbols() const
      -> const std::unordered_map<slang::SourceLocation, SymbolInfo>&;

 private:
  explicit SemanticIndex() = default;

  // Core data storage
  std::unordered_map<slang::SourceLocation, SymbolInfo> symbols_;

  // Visitor for symbol collection (moved from separate file)
  template <typename TCallback>
  class IndexVisitor
      : public slang::ast::ASTVisitor<IndexVisitor<TCallback>, true, true> {
   public:
    explicit IndexVisitor(TCallback callback) : callback_(std::move(callback)) {
    }

    // Universal pre-visit hook for symbols
    template <typename T>
    void preVisit(const T& symbol) {
      if constexpr (std::is_base_of_v<slang::ast::Symbol, T>) {
        callback_(symbol);
      }
    }

    // Default traversal
    template <typename T>
    void handle(const T& node) {
      this->visitDefault(node);
    }

   private:
    TCallback callback_;
  };

  // Utility methods ported from existing indexes
  static auto UnwrapSymbol(const slang::ast::Symbol& symbol)
      -> const slang::ast::Symbol&;

  static auto ConvertToLspKind(const slang::ast::Symbol& symbol)
      -> lsp::SymbolKind;

  static auto ComputeLspRange(
      const slang::ast::Symbol& symbol,
      const slang::SourceManager& source_manager) -> lsp::Range;

  static auto ShouldIndex(const slang::ast::Symbol& symbol) -> bool;
};

}  // namespace slangd::semantic

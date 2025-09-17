#pragma once

#include <slang/ast/ASTVisitor.h>
#include <slang/ast/Symbol.h>

namespace slangd::semantic {

// Visitor that collects all symbols using the preVisit hook for LSP semantic
// indexing
template <typename TCallback>
class SemanticIndexVisitor : public slang::ast::ASTVisitor<
                                 SemanticIndexVisitor<TCallback>, true, true> {
 public:
  explicit SemanticIndexVisitor(TCallback callback)
      : callback_(std::move(callback)) {
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

}  // namespace slangd::semantic

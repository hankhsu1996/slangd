#include "slangd/syntax/syntax_document_symbol_visitor.hpp"

#include <slang/syntax/AllSyntax.h>

#include "slangd/utils/conversion.hpp"

namespace slangd::syntax {

SyntaxDocumentSymbolVisitor::SyntaxDocumentSymbolVisitor(
    std::string current_file_uri, const slang::SourceManager& source_manager,
    slang::BufferID main_buffer_id)
    : current_file_uri_(std::move(current_file_uri)),
      source_manager_(std::cref(source_manager)),
      main_buffer_id_(main_buffer_id) {
}

auto SyntaxDocumentSymbolVisitor::GetResult()
    -> std::vector<lsp::DocumentSymbol> {
  return std::move(roots_);
}

auto SyntaxDocumentSymbolVisitor::BuildDocumentSymbol(
    std::string_view name, lsp::SymbolKind kind, slang::SourceRange range,
    slang::SourceRange selection_range) -> lsp::DocumentSymbol {
  return lsp::DocumentSymbol{
      .name = std::string(name),
      .detail = std::nullopt,
      .kind = kind,
      .tags = std::nullopt,
      .deprecated = std::nullopt,
      .range = slangd::ToLspRange(range, source_manager_.get()),
      .selectionRange =
          slangd::ToLspRange(selection_range, source_manager_.get()),
      .children = std::vector<lsp::DocumentSymbol>{}};
}

auto SyntaxDocumentSymbolVisitor::IsInCurrentFile(slang::SourceRange range)
    -> bool {
  if (!range.start()) {
    return false;
  }
  return range.start().buffer() == main_buffer_id_;
}

auto SyntaxDocumentSymbolVisitor::AddToParent(lsp::DocumentSymbol symbol)
    -> void {
  if (parent_stack_.empty()) {
    roots_.push_back(std::move(symbol));
  } else {
    parent_stack_.back()->children->push_back(std::move(symbol));
  }
}

auto SyntaxDocumentSymbolVisitor::GetLastAddedSymbol() -> lsp::DocumentSymbol* {
  if (parent_stack_.empty()) {
    return &roots_.back();
  }
  return &parent_stack_.back()->children->back();
}

void SyntaxDocumentSymbolVisitor::visitDefault(
    const slang::syntax::SyntaxNode& node) {
  if (!IsInCurrentFile(node.sourceRange())) {
    return;
  }

  for (uint32_t i = 0; i < node.getChildCount(); i++) {
    const auto* child = node.childNode(i);
    if (child != nullptr) {
      child->visit(*this);
    }
  }
}

void SyntaxDocumentSymbolVisitor::handle(
    const slang::syntax::ModuleDeclarationSyntax& syntax) {
  auto doc_symbol = BuildDocumentSymbol(
      syntax.header->name.valueText(), lsp::SymbolKind::kModule,
      syntax.sourceRange(), syntax.header->name.range());
  AddToParent(std::move(doc_symbol));

  parent_stack_.push_back(GetLastAddedSymbol());
  visitDefault(syntax);
  parent_stack_.pop_back();
}

void SyntaxDocumentSymbolVisitor::handle(
    const slang::syntax::ClassDeclarationSyntax& syntax) {
  auto doc_symbol = BuildDocumentSymbol(
      syntax.name.valueText(), lsp::SymbolKind::kClass, syntax.sourceRange(),
      syntax.name.range());
  AddToParent(std::move(doc_symbol));

  parent_stack_.push_back(GetLastAddedSymbol());
  visitDefault(syntax);
  parent_stack_.pop_back();
}

void SyntaxDocumentSymbolVisitor::handle(
    const slang::syntax::DataDeclarationSyntax& syntax) {
  for (const auto& declarator : syntax.declarators) {
    if (!declarator->name.valueText().empty()) {
      auto doc_symbol = BuildDocumentSymbol(
          declarator->name.valueText(), lsp::SymbolKind::kVariable,
          declarator->sourceRange(), declarator->name.range());
      AddToParent(std::move(doc_symbol));
    }
  }
}

void SyntaxDocumentSymbolVisitor::handle(
    const slang::syntax::TypedefDeclarationSyntax& syntax) {
  auto doc_symbol = BuildDocumentSymbol(
      syntax.name.valueText(), lsp::SymbolKind::kClass, syntax.sourceRange(),
      syntax.name.range());
  AddToParent(std::move(doc_symbol));

  if (syntax.type != nullptr) {
    parent_stack_.push_back(GetLastAddedSymbol());
    visitDefault(syntax);
    parent_stack_.pop_back();
  }
}

}  // namespace slangd::syntax

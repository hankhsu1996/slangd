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
  // ModuleDeclarationSyntax handles three syntax kinds with identical structure
  lsp::SymbolKind kind = lsp::SymbolKind::kModule;
  switch (syntax.kind) {
    case slang::syntax::SyntaxKind::PackageDeclaration:
      kind = lsp::SymbolKind::kPackage;
      break;
    case slang::syntax::SyntaxKind::InterfaceDeclaration:
      kind = lsp::SymbolKind::kInterface;
      break;
    default:
      break;
  }

  auto doc_symbol = BuildDocumentSymbol(
      syntax.header->name.valueText(), kind, syntax.header->name.range(),
      syntax.header->name.range());
  AddToParent(std::move(doc_symbol));

  parent_stack_.push_back(GetLastAddedSymbol());
  visitDefault(syntax);
  parent_stack_.pop_back();
}

void SyntaxDocumentSymbolVisitor::handle(
    const slang::syntax::ClassDeclarationSyntax& syntax) {
  auto doc_symbol = BuildDocumentSymbol(
      syntax.name.valueText(), lsp::SymbolKind::kClass, syntax.name.range(),
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
          declarator->name.range(), declarator->name.range());
      AddToParent(std::move(doc_symbol));
    }
  }
}

void SyntaxDocumentSymbolVisitor::handle(
    const slang::syntax::TypedefDeclarationSyntax& syntax) {
  lsp::SymbolKind kind = lsp::SymbolKind::kClass;
  if (syntax.type != nullptr) {
    if (syntax.type->kind == slang::syntax::SyntaxKind::EnumType) {
      kind = lsp::SymbolKind::kEnum;
    } else if (syntax.type->kind == slang::syntax::SyntaxKind::StructType) {
      kind = lsp::SymbolKind::kStruct;
    }
  }
  auto doc_symbol = BuildDocumentSymbol(
      syntax.name.valueText(), kind, syntax.name.range(), syntax.name.range());
  AddToParent(std::move(doc_symbol));

  if (syntax.type != nullptr) {
    parent_stack_.push_back(GetLastAddedSymbol());
    visitDefault(syntax);
    parent_stack_.pop_back();
  }
}

void SyntaxDocumentSymbolVisitor::handle(
    const slang::syntax::FunctionDeclarationSyntax& syntax) {
  auto name_token = syntax.prototype->name->getLastToken();
  auto doc_symbol = BuildDocumentSymbol(
      name_token.valueText(), lsp::SymbolKind::kFunction, name_token.range(),
      name_token.range());
  AddToParent(std::move(doc_symbol));
}

void SyntaxDocumentSymbolVisitor::handle(
    const slang::syntax::EnumTypeSyntax& syntax) {
  for (const auto& member : syntax.members) {
    if (member->name.valueText().empty()) {
      continue;
    }
    auto doc_symbol = BuildDocumentSymbol(
        member->name.valueText(), lsp::SymbolKind::kEnumMember,
        member->sourceRange(), member->name.range());
    AddToParent(std::move(doc_symbol));
  }
}

void SyntaxDocumentSymbolVisitor::handle(
    const slang::syntax::StructUnionTypeSyntax& syntax) {
  for (const auto& member : syntax.members) {
    for (const auto& declarator : member->declarators) {
      if (declarator->name.valueText().empty()) {
        continue;
      }
      auto doc_symbol = BuildDocumentSymbol(
          declarator->name.valueText(), lsp::SymbolKind::kField,
          declarator->sourceRange(), declarator->name.range());
      AddToParent(std::move(doc_symbol));
    }
  }
}

void SyntaxDocumentSymbolVisitor::handle(
    const slang::syntax::ImplicitAnsiPortSyntax& syntax) {
  if (syntax.declarator != nullptr &&
      !syntax.declarator->name.valueText().empty()) {
    auto doc_symbol = BuildDocumentSymbol(
        syntax.declarator->name.valueText(), lsp::SymbolKind::kVariable,
        syntax.declarator->sourceRange(), syntax.declarator->name.range());
    AddToParent(std::move(doc_symbol));
  }
}

void SyntaxDocumentSymbolVisitor::handle(
    const slang::syntax::ParameterDeclarationSyntax& syntax) {
  for (const auto& declarator : syntax.declarators) {
    if (!declarator->name.valueText().empty()) {
      auto doc_symbol = BuildDocumentSymbol(
          declarator->name.valueText(), lsp::SymbolKind::kConstant,
          declarator->name.range(), declarator->name.range());
      AddToParent(std::move(doc_symbol));
    }
  }
}

void SyntaxDocumentSymbolVisitor::handle(
    const slang::syntax::NetDeclarationSyntax& syntax) {
  for (const auto& declarator : syntax.declarators) {
    if (!declarator->name.valueText().empty()) {
      auto doc_symbol = BuildDocumentSymbol(
          declarator->name.valueText(), lsp::SymbolKind::kVariable,
          declarator->name.range(), declarator->name.range());
      AddToParent(std::move(doc_symbol));
    }
  }
}

}  // namespace slangd::syntax

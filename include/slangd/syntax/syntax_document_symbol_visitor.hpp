#pragma once

#include <optional>
#include <string>
#include <vector>

#include <lsp/document_features.hpp>
#include <slang/syntax/SyntaxVisitor.h>
#include <slang/text/SourceManager.h>

namespace slangd::syntax {

// Syntax-based document symbol visitor for LSP documentSymbol requests
// Traverses syntax tree directly without semantic elaboration
// Special case: This is the only LSP feature using syntax instead of semantic
class SyntaxDocumentSymbolVisitor
    : public slang::syntax::SyntaxVisitor<SyntaxDocumentSymbolVisitor> {
 public:
  SyntaxDocumentSymbolVisitor(
      std::string current_file_uri, const slang::SourceManager& source_manager,
      slang::BufferID main_buffer_id);

  auto GetResult() -> std::vector<lsp::DocumentSymbol>;

  void visitDefault(const slang::syntax::SyntaxNode& node);

  // Handle different syntax types (add as needed based on tests)
  void handle(const slang::syntax::ModuleDeclarationSyntax& syntax);
  void handle(const slang::syntax::ClassDeclarationSyntax& syntax);
  void handle(const slang::syntax::DataDeclarationSyntax& syntax);
  void handle(const slang::syntax::TypedefDeclarationSyntax& syntax);
  void handle(const slang::syntax::FunctionDeclarationSyntax& syntax);
  void handle(const slang::syntax::EnumTypeSyntax& syntax);
  void handle(const slang::syntax::StructUnionTypeSyntax& syntax);
  void handle(const slang::syntax::ImplicitAnsiPortSyntax& syntax);
  void handle(const slang::syntax::ParameterDeclarationSyntax& syntax);
  void handle(const slang::syntax::NetDeclarationSyntax& syntax);

 private:
  std::vector<lsp::DocumentSymbol> roots_;
  std::vector<lsp::DocumentSymbol*> parent_stack_;
  std::string current_file_uri_;
  std::reference_wrapper<const slang::SourceManager> source_manager_;
  slang::BufferID main_buffer_id_;

  auto BuildDocumentSymbol(
      std::string_view name, lsp::SymbolKind kind, slang::SourceRange range,
      slang::SourceRange selection_range) -> lsp::DocumentSymbol;

  auto IsInCurrentFile(slang::SourceRange range) -> bool;
  auto AddToParent(lsp::DocumentSymbol symbol)
      -> std::optional<std::reference_wrapper<lsp::DocumentSymbol>>;
  auto AddToParentWithChildren(
      lsp::DocumentSymbol symbol, const slang::syntax::SyntaxNode& syntax_node)
      -> void;
};

}  // namespace slangd::syntax

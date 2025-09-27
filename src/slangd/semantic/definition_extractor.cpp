#include "slangd/semantic/definition_extractor.hpp"

#include <slang/syntax/AllSyntax.h>
#include <slang/syntax/SyntaxFacts.h>
#include <spdlog/spdlog.h>

namespace slangd::semantic {

auto DefinitionExtractor::ExtractDefinitionRange(
    const slang::ast::Symbol& symbol, const slang::syntax::SyntaxNode& syntax)
    -> slang::SourceRange {
  using SK = slang::ast::SymbolKind;
  using SyntaxKind = slang::syntax::SyntaxKind;

  // Extract precise name range based on symbol and syntax type.
  // This function is safe to call with any symbol/syntax combination -
  // it will extract the precise range when possible, or fall back to
  // the full syntax range, ensuring a valid range is always returned.
  switch (symbol.kind) {
    case SK::Package:
      if (syntax.kind == SyntaxKind::PackageDeclaration) {
        return syntax.as<slang::syntax::ModuleDeclarationSyntax>()
            .header->name.range();
      }
      break;

    case SK::Definition: {
      if (syntax.kind == SyntaxKind::ModuleDeclaration) {
        return syntax.as<slang::syntax::ModuleDeclarationSyntax>()
            .header->name.range();
      }
      break;
    }

    case SK::TypeAlias:
      if (syntax.kind == SyntaxKind::TypedefDeclaration) {
        return syntax.as<slang::syntax::TypedefDeclarationSyntax>()
            .name.range();
      }
      break;

    case SK::Variable:
      return syntax.sourceRange();  // Variables use full declaration range

    case SK::Parameter:
      if (syntax.kind == SyntaxKind::Declarator) {
        return syntax.as<slang::syntax::DeclaratorSyntax>().name.range();
      }
      break;

    case SK::StatementBlock: {
      if (syntax.kind == SyntaxKind::SequentialBlockStatement ||
          syntax.kind == SyntaxKind::ParallelBlockStatement) {
        return ExtractStatementBlockRange(syntax);
      }
      break;
    }

    case SK::Subroutine:
      if (syntax.kind == SyntaxKind::TaskDeclaration ||
          syntax.kind == SyntaxKind::FunctionDeclaration) {
        // Both task and function declarations use FunctionDeclarationSyntax
        const auto& func_syntax =
            syntax.as<slang::syntax::FunctionDeclarationSyntax>();
        if (func_syntax.prototype && func_syntax.prototype->name) {
          return func_syntax.prototype->name->sourceRange();
        }
      }
      break;

    case SK::EnumValue:
      // Enum values have their name directly accessible through syntax
      return syntax.sourceRange();

    case SK::Field:
      // Struct/union field symbols
      return syntax.sourceRange();

    case SK::Net:
      // Net symbols - extract name from declarator syntax
      if (syntax.kind == SyntaxKind::Declarator) {
        return syntax.as<slang::syntax::DeclaratorSyntax>().name.range();
      }
      return syntax.sourceRange();

    case SK::Port:
      // Port symbols - handle different ANSI and non-ANSI syntax types
      if (syntax.kind == SyntaxKind::ImplicitAnsiPort) {
        return syntax.as<slang::syntax::ImplicitAnsiPortSyntax>()
            .declarator->name.range();
      } else if (syntax.kind == SyntaxKind::ExplicitAnsiPort) {
        return syntax.as<slang::syntax::ExplicitAnsiPortSyntax>().name.range();
      } else if (syntax.kind == SyntaxKind::PortDeclaration) {
        const auto& decl_syntax =
            syntax.as<slang::syntax::PortDeclarationSyntax>();
        if (!decl_syntax.declarators.empty()) {
          return decl_syntax.declarators[0]->name.range();
        }
      }
      return syntax.sourceRange();

    default:
      // For symbol types without specific handling, fall back to full syntax
      // range
      break;
  }

  // Safe fallback: return the full syntax range when precise extraction isn't
  // possible. This ensures every symbol gets a valid, clickable range for
  // go-to-definition.
  return syntax.sourceRange();
}

auto DefinitionExtractor::ExtractStatementBlockRange(
    const slang::syntax::SyntaxNode& syntax) -> slang::SourceRange {
  const auto& block_syntax = syntax.as<slang::syntax::BlockStatementSyntax>();
  if (block_syntax.blockName != nullptr) {
    return block_syntax.blockName->name.range();
  }
  // Fallback to syntax range if no block name
  return syntax.sourceRange();
}

}  // namespace slangd::semantic

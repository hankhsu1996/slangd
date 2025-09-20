#include "slangd/semantic/definition_extractor.hpp"

#include <slang/syntax/AllSyntax.h>

namespace slangd::semantic {

auto DefinitionExtractor::ExtractDefinitionRange(
    const slang::ast::Symbol& symbol, const slang::syntax::SyntaxNode& syntax)
    -> slang::SourceRange {
  using SK = slang::ast::SymbolKind;
  using SyntaxKind = slang::syntax::SyntaxKind;

  // Extract precise name range based on symbol and syntax type
  switch (symbol.kind) {
    case SK::Package:
      if (syntax.kind == SyntaxKind::PackageDeclaration) {
        return ExtractPackageRange(syntax);
      }
      break;

    case SK::Definition: {
      if (syntax.kind == SyntaxKind::ModuleDeclaration) {
        return ExtractModuleRange(syntax);
      }
      break;
    }

    case SK::TypeAlias:
      if (syntax.kind == SyntaxKind::TypedefDeclaration) {
        return ExtractTypedefRange(syntax);
      }
      break;

    case SK::Variable:
      return ExtractVariableRange(syntax);

    case SK::Parameter:
      return ExtractParameterRange(syntax);

    case SK::StatementBlock: {
      if (syntax.kind == SyntaxKind::SequentialBlockStatement ||
          syntax.kind == SyntaxKind::ParallelBlockStatement) {
        return ExtractStatementBlockRange(syntax);
      }
      break;
    }

    default:
      // For most symbol types, use the syntax source range
      break;
  }

  // Default fallback: use the syntax node's source range
  return syntax.sourceRange();
}

auto DefinitionExtractor::ExtractPackageRange(
    const slang::syntax::SyntaxNode& syntax) -> slang::SourceRange {
  const auto& pkg_syntax = syntax.as<slang::syntax::ModuleDeclarationSyntax>();
  return pkg_syntax.header->name.range();
}

auto DefinitionExtractor::ExtractModuleRange(
    const slang::syntax::SyntaxNode& syntax) -> slang::SourceRange {
  const auto& mod_syntax = syntax.as<slang::syntax::ModuleDeclarationSyntax>();
  return mod_syntax.header->name.range();
}

auto DefinitionExtractor::ExtractTypedefRange(
    const slang::syntax::SyntaxNode& syntax) -> slang::SourceRange {
  const auto& typedef_syntax =
      syntax.as<slang::syntax::TypedefDeclarationSyntax>();
  return typedef_syntax.name.range();
}

auto DefinitionExtractor::ExtractVariableRange(
    const slang::syntax::SyntaxNode& syntax) -> slang::SourceRange {
  // For variables, use the entire syntax range as name range
  return syntax.sourceRange();
}

auto DefinitionExtractor::ExtractParameterRange(
    const slang::syntax::SyntaxNode& syntax) -> slang::SourceRange {
  // TODO(hankhsu): Extract precise parameter name range instead of full
  // declaration Currently returns the full syntax range which includes "WIDTH =
  // 8" instead of just "WIDTH" This is acceptable for now since
  // go-to-definition functionality works Future enhancement: Parse parameter
  // declaration syntax to extract just the name token

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

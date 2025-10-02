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
  //
  // PURPOSE: This extractor is for POLYMORPHIC cases where the symbol type
  // is not known at compile time (e.g., expressions that can reference
  // variables, functions, or other symbol types).
  //
  // For symbol-specific handlers (like TypeAliasType::handle), prefer
  // extracting the range directly in the handler since you already know
  // the exact syntax type.
  //
  // This function is safe to call with any symbol/syntax combination -
  // it will extract the precise range when possible, or fall back to
  // symbol.location + name.length(), ensuring a valid range is always returned.
  switch (symbol.kind) {
    case SK::Package:
      if (syntax.kind == SyntaxKind::PackageDeclaration) {
        return syntax.as<slang::syntax::ModuleDeclarationSyntax>()
            .header->name.range();
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
        if ((func_syntax.prototype != nullptr) &&
            (func_syntax.prototype->name != nullptr)) {
          return func_syntax.prototype->name->sourceRange();
        }
      }
      break;

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

    case SK::GenerateBlockArray:
      // Generate block array (for loop) - extract name from loop generate block
      if (syntax.kind == SyntaxKind::LoopGenerate) {
        const auto& loop_gen = syntax.as<slang::syntax::LoopGenerateSyntax>();
        if (loop_gen.block->kind == SyntaxKind::GenerateBlock) {
          const auto& gen_block =
              loop_gen.block->as<slang::syntax::GenerateBlockSyntax>();
          if (gen_block.beginName != nullptr) {
            return gen_block.beginName->name.range();
          }
        }
      }
      return syntax.sourceRange();

    case SK::Field:
      // Struct/union field declarators
      if (syntax.kind == SyntaxKind::Declarator) {
        return syntax.as<slang::syntax::DeclaratorSyntax>().name.range();
      }
      break;

    default:
      // Unhandled symbol type - log warning and use fallback
      spdlog::warn(
          "DefinitionExtractor: Unhandled symbol kind '{}' with syntax kind "
          "'{}' for symbol '{}'. Using symbol.location + name.length() "
          "fallback. Consider adding explicit handling.",
          slang::ast::toString(symbol.kind),
          slang::syntax::toString(syntax.kind), symbol.name);
      break;
  }

  // Fallback: use symbol location + name length
  // This should only trigger for unhandled symbol types (warning logged above)
  if (symbol.location.valid()) {
    return slang::SourceRange(
        symbol.location, symbol.location + symbol.name.length());
  }

  // Should never reach here - symbol with syntax but no valid location
  spdlog::error(
      "DefinitionExtractor: Symbol '{}' has syntax but invalid location",
      symbol.name);
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

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

    case SK::InterfacePort:
      // Interface port symbols - extract name from interface port header
      if (syntax.kind == SyntaxKind::InterfacePortHeader) {
        return syntax.as<slang::syntax::InterfacePortHeaderSyntax>()
            .nameOrKeyword.range();
      }
      return syntax.sourceRange();

    case SK::Modport:
      // Modport symbols - extract name from modport item
      if (syntax.kind == SyntaxKind::ModportItem) {
        return syntax.as<slang::syntax::ModportItemSyntax>().name.range();
      }
      return syntax.sourceRange();

    case SK::ModportPort:
      // Modport port symbols - extract name from modport named port
      if (syntax.kind == SyntaxKind::ModportNamedPort) {
        return syntax.as<slang::syntax::ModportNamedPortSyntax>().name.range();
      }
      return syntax.sourceRange();

    case SK::GenerateBlock:
      // Named generate block - extract name from begin block
      if (syntax.kind == SyntaxKind::GenerateBlock) {
        const auto& gen_block = syntax.as<slang::syntax::GenerateBlockSyntax>();
        if (gen_block.beginName != nullptr) {
          return gen_block.beginName->name.range();
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

    case SK::Genvar:
      // Genvar declaration - extract name from identifier list
      if (syntax.kind == SyntaxKind::GenvarDeclaration) {
        const auto& genvar_decl =
            syntax.as<slang::syntax::GenvarDeclarationSyntax>();
        // Find the specific genvar name by matching the symbol name
        for (const auto& identifier : genvar_decl.identifiers) {
          if (identifier->identifier.valueText() == symbol.name) {
            return identifier->identifier.range();
          }
        }
      }
      // Handle inline genvar in loop generate
      if (syntax.kind == SyntaxKind::LoopGenerate) {
        const auto& loop_gen = syntax.as<slang::syntax::LoopGenerateSyntax>();
        if (loop_gen.genvar.valueText() == "genvar") {
          return loop_gen.identifier.range();
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

#pragma once

#include <slang/ast/Symbol.h>
#include <slang/syntax/SyntaxNode.h>
#include <slang/text/SourceLocation.h>

namespace slangd::semantic {

// DefinitionExtractor handles precise definition range extraction for each
// symbol type. Each symbol kind requires specific syntax analysis to extract
// the exact name range for go-to-definition functionality.
class DefinitionExtractor {
 public:
  // Main extraction method that delegates to symbol-type specialists
  static auto ExtractDefinitionRange(
      const slang::ast::Symbol& symbol, const slang::syntax::SyntaxNode& syntax)
      -> slang::SourceRange;

 private:
  // Symbol-type specific extraction methods
  static auto ExtractPackageRange(const slang::syntax::SyntaxNode& syntax)
      -> slang::SourceRange;
  static auto ExtractModuleRange(const slang::syntax::SyntaxNode& syntax)
      -> slang::SourceRange;
  static auto ExtractTypedefRange(const slang::syntax::SyntaxNode& syntax)
      -> slang::SourceRange;
  static auto ExtractVariableRange(const slang::syntax::SyntaxNode& syntax)
      -> slang::SourceRange;
  static auto ExtractParameterRange(const slang::syntax::SyntaxNode& syntax)
      -> slang::SourceRange;
  static auto ExtractStatementBlockRange(
      const slang::syntax::SyntaxNode& syntax) -> slang::SourceRange;
};

}  // namespace slangd::semantic

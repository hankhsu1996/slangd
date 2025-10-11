#pragma once

#include <memory>

#include <slang/ast/Symbol.h>
#include <slang/syntax/SyntaxNode.h>
#include <slang/text/SourceLocation.h>
#include <spdlog/spdlog.h>

namespace slangd::semantic {

// DefinitionExtractor handles precise definition range extraction for each
// symbol type. Each symbol kind requires specific syntax analysis to extract
// the exact name range for go-to-definition functionality.
class DefinitionExtractor {
 public:
  explicit DefinitionExtractor(std::shared_ptr<spdlog::logger> logger)
      : logger_(logger ? logger : spdlog::default_logger()) {
  }

  // Extract the most precise definition range for a symbol.
  // Always returns a valid range - either the precise name range when possible,
  // or the full syntax range as a safe fallback. Safe to call with any
  // symbol/syntax combination.
  auto ExtractDefinitionRange(
      const slang::ast::Symbol& symbol, const slang::syntax::SyntaxNode& syntax)
      -> slang::SourceRange;

 private:
  // Symbol-type specific extraction method for complex cases
  auto static ExtractStatementBlockRange(
      const slang::syntax::SyntaxNode& syntax) -> slang::SourceRange;

  // Logger for warnings and errors
  std::shared_ptr<spdlog::logger> logger_;
};

}  // namespace slangd::semantic

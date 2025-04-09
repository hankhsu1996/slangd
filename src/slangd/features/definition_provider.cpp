#include "slangd/features/definition_provider.hpp"

#include <slang/ast/ASTVisitor.h>
#include <slang/syntax/SyntaxVisitor.h>
#include <spdlog/spdlog.h>

namespace slangd {

class GenericAstLocatorVisitor
    : public slang::ast::ASTVisitor<GenericAstLocatorVisitor, true, true> {
 public:
  GenericAstLocatorVisitor(slang::SourceLocation target)
      : bestNode(nullptr),
        target(target),
        smallestRangeSize(std::numeric_limits<size_t>::max()) {
  }

  template <typename T>
  void handle(const T& node) {
    if constexpr (std::is_base_of_v<slang::ast::Symbol, T>) {
      slang::SourceRange range = getNodeRange(node);

      if (rangeValid(range) && range.contains(target)) {
        size_t size = rangeSize(range);
        if (size < smallestRangeSize) {
          smallestRangeSize = size;
          bestNode = &node;
        }
      }
    }

    visitDefault(node);
  }

  const slang::ast::Symbol* bestNode = nullptr;

 private:
  slang::SourceLocation target;
  size_t smallestRangeSize;

  bool rangeValid(const slang::SourceRange& range) const {
    return range.start().valid() && range.end().valid() &&
           range.start().buffer() == range.end().buffer();
  }

  size_t rangeSize(const slang::SourceRange& range) const {
    return range.end().offset() - range.start().offset();
  }

  template <typename T>
  slang::SourceRange getNodeRange(const T& node) const {
    if constexpr (requires { node.sourceRange; }) {
      return node.sourceRange;
    } else if constexpr (requires { node.location; }) {
      return slang::SourceRange(node.location, node.location);
    } else {
      return slang::SourceRange::NoLocation;
    }
  }
};

const slang::syntax::SyntaxNode* FindSmallestEnclosingNode(
    const slang::syntax::SyntaxNode* node, slang::SourceLocation location) {
  if (!node || !node->sourceRange().contains(location)) return nullptr;

  const slang::syntax::SyntaxNode* result = node;
  size_t count = node->getChildCount();

  for (size_t i = 0; i < count; ++i) {
    const slang::syntax::SyntaxNode* child = node->childNode(i);
    if (!child) continue;

    const slang::syntax::SyntaxNode* match =
        FindSmallestEnclosingNode(child, location);
    if (match) result = match;
  }

  return result;
}

auto DefinitionProvider::GetDefinitionForUri(
    std::string uri, lsp::Position position)
    -> asio::awaitable<std::vector<lsp::Location>> {
  auto compilation = document_manager_->GetCompilation(uri);
  auto syntax_tree = document_manager_->GetSyntaxTree(uri);
  auto source_manager = document_manager_->GetSourceManager(uri);
  logger_->info("DefinitionProvider get definition for uri: {}", uri);

  if (!compilation || !syntax_tree || !source_manager) {
    logger_->error("Failed to get compilation, syntax tree, or source manager");
    co_return std::vector<lsp::Location>{};
  }

  auto buffers = source_manager->getAllBuffers();
  if (buffers.empty()) {
    logger_->error("No buffers found for URI: {}", uri);
    co_return std::vector<lsp::Location>{};
  }
  auto buffer = buffers[0];
  auto source_text = source_manager->getSourceText(buffer);

  size_t offset = std::string::npos;
  int current_line = 0;
  int current_column = 0;

  for (size_t i = 0; i < source_text.size(); ++i) {
    if (current_line == position.line && current_column == position.character) {
      offset = i;
      break;
    }

    if (source_text[i] == '\n') {
      current_line++;
      current_column = 0;
    } else {
      current_column++;
    }
  }

  auto location = slang::SourceLocation(buffer, offset);
  logger_->info(
      "Translated line: {} col: {} => offset: {}", position.line,
      position.character, offset);

  GenericAstLocatorVisitor visitor(location);
  for (auto& unit : compilation->getCompilationUnits()) {
    logger_->info("Visiting compilation unit");
    unit->visit(visitor);
  }

  if (visitor.bestNode) {
    logger_->info("Found node: {}", visitor.bestNode->name);
  } else {
    logger_->info("No node found");
  }

  co_return std::vector<lsp::Location>{};
}

auto DefinitionProvider::ResolveDefinitionFromCompilation(
    slang::ast::Compilation& compilation,
    const std::shared_ptr<slang::SourceManager>& source_manager,
    const slang::ast::Symbol& symbol)
    -> asio::awaitable<std::vector<lsp::Location>> {
  co_return std::vector<lsp::Location>{};
}

}  // namespace slangd

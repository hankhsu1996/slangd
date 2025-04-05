#include "slangd/utils/conversion.hpp"

namespace slangd {

lsp::Range ConvertSlangLocationToLspRange(
    const slang::SourceLocation& location,
    const std::shared_ptr<slang::SourceManager>& source_manager) {
  if (!location) return lsp::Range{};

  auto line = source_manager->getLineNumber(location);
  auto column = source_manager->getColumnNumber(location);

  return lsp::Range{
      lsp::Position{static_cast<int>(line - 1), static_cast<int>(column - 1)},
      lsp::Position{static_cast<int>(line - 1), static_cast<int>(column - 1)}};
}

lsp::Range ConvertSlangRangesToLspRange(
    const std::vector<slang::SourceRange>& ranges,
    const std::shared_ptr<slang::SourceManager>& source_manager) {
  if (ranges.size() == 0) return lsp::Range{};

  slang::SourceLocation start = ranges[0].start();
  slang::SourceLocation end = ranges[0].end();

  auto line = source_manager->getLineNumber(start);
  auto column = source_manager->getColumnNumber(start);

  // Create a range that spans a single character at the location
  lsp::Position start_pos{
      static_cast<int>(line - 1),   // Convert to 0-based
      static_cast<int>(column - 1)  // Convert to 0-based
  };

  auto end_line = source_manager->getLineNumber(end);
  auto end_column = source_manager->getColumnNumber(end);

  lsp::Position end_pos{
      static_cast<int>(end_line - 1),   // Convert to 0-based
      static_cast<int>(end_column - 1)  // Convert to 0-based
  };

  // For now, we set end position same as start for a single point range
  return lsp::Range{start_pos, end_pos};
}

}  // namespace slangd

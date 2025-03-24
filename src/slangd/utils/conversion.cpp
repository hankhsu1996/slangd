#include "slangd/utils/conversion.hpp"

namespace slangd {

lsp::Range ConvertSlangLocationToLspRange(
    const slang::SourceLocation& location,
    const std::shared_ptr<slang::SourceManager>& source_manager) {
  if (!location) return lsp::Range{};

  auto line = source_manager->getLineNumber(location);
  auto column = source_manager->getColumnNumber(location);

  // Create a range that spans a single character at the location
  lsp::Position start_pos{
      static_cast<int>(line - 1),   // Convert to 0-based
      static_cast<int>(column - 1)  // Convert to 0-based
  };

  // For now, we set end position same as start for a single point range
  return lsp::Range{start_pos, start_pos};
}

}  // namespace slangd

#include "slangd/utils/conversion.hpp"

namespace slangd {

auto ConvertSlangLocationToLspRange(
    const slang::SourceLocation& location,
    const slang::SourceManager& source_manager) -> lsp::Range {
  if (!location) {
    return lsp::Range{};
  }

  auto line = source_manager.getLineNumber(location);
  auto column = source_manager.getColumnNumber(location);

  // Convert to single-point LSP position (0-based)
  auto position = lsp::Position{
      .line = static_cast<int>(line - 1),
      .character = static_cast<int>(column - 1)};

  // Return a zero-length range at this position
  return lsp::Range{.start = position, .end = position};
}

auto ConvertSlangRangeToLspRange(
    const slang::SourceRange& range, const slang::SourceManager& source_manager)
    -> lsp::Range {
  if (!range.start() || !range.end()) {
    return lsp::Range{};
  }

  // Convert start position
  auto start_line = source_manager.getLineNumber(range.start());
  auto start_column = source_manager.getColumnNumber(range.start());
  lsp::Position start_pos{
      .line = static_cast<int>(start_line - 1),
      .character = static_cast<int>(start_column - 1)};

  // Convert end position
  auto end_line = source_manager.getLineNumber(range.end());
  auto end_column = source_manager.getColumnNumber(range.end());
  lsp::Position end_pos{
      .line = static_cast<int>(end_line - 1),
      .character = static_cast<int>(end_column - 1)};

  return lsp::Range{.start = start_pos, .end = end_pos};
}

auto ConvertLspPositionToSlangLocation(
    const lsp::Position& position, const slang::BufferID& buffer_id,
    const slang::SourceManager& source_manager) -> slang::SourceLocation {
  // Get the text content for this buffer
  std::string_view text = source_manager.getSourceText(buffer_id);
  if (text.empty()) {
    return {};
  }

  // Convert LSP position (line, character) to source offset
  // Note: LSP positions are 0-based (line 0, column 0 is the first character)
  // We need to calculate the byte offset into the buffer for Slang
  size_t offset = 0;
  int current_line = 0;

  // First find the start of the target line
  std::string_view::size_type line_start = 0;
  for (std::string_view::size_type i = 0;
       i < text.size() && current_line < position.line; ++i) {
    if (text[i] == '\n') {
      current_line++;
      line_start = i + 1;
    }
  }

  // If we found the correct line, add the character position
  if (current_line == position.line) {
    // Ensure we don't go past the end of the text
    int chars_on_line = 0;
    std::string_view::size_type i = line_start;

    // Count characters until we reach the target character or end of line
    while (i < text.size() && chars_on_line < position.character &&
           text[i] != '\n') {
      i++;
      chars_on_line++;
    }

    offset = i;
  } else {
    // If we couldn't find the line, return the end of the document
    offset = text.size();
  }

  // Create and return the Slang source location
  return {buffer_id, offset};
}

auto ConvertSlangLocationToLspLocation(
    const slang::SourceLocation& location,
    const slang::SourceManager& source_manager) -> lsp::Location {
  if (!location) {
    return lsp::Location{};
  }

  // Get the file name from the buffer
  auto file_name = source_manager.getFileName(location);

  // Create a range at this position
  auto range = ConvertSlangLocationToLspRange(location, source_manager);

  // Create and return the LSP location
  return lsp::Location{.uri = lsp::DocumentUri(file_name), .range = range};
}

auto ConvertSlangLocationToLspPosition(
    const slang::SourceLocation& location,
    const slang::SourceManager& source_manager) -> lsp::Position {
  if (!location) {
    return {};
  }

  // Extract line and column information from Slang source location
  // Note: Slang line/column are 1-based but LSP expects 0-based positions
  auto line = source_manager.getLineNumber(location);
  auto column = source_manager.getColumnNumber(location);

  // Convert to LSP position (0-based line and column)
  return lsp::Position{
      .line = static_cast<int>(line - 1),
      .character = static_cast<int>(column - 1)};
}

}  // namespace slangd

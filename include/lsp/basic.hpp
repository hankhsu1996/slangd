#pragma once

#include <nlohmann/json.hpp>

namespace lsp {

/**
 * Position in a document (zero-based)
 */
struct Position {
  int line;
  int character;
};

/**
 * Range in a document (positions are zero-based)
 */
struct Range {
  Position start;
  Position end;
};

// JSON conversion functions
void to_json(nlohmann::json& j, const Position& p);
void from_json(const nlohmann::json& j, Position& p);

void to_json(nlohmann::json& j, const Range& r);
void from_json(const nlohmann::json& j, Range& r);

}  // namespace lsp

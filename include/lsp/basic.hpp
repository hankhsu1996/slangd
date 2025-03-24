#pragma once

#include <nlohmann/json.hpp>

namespace lsp {

/**
 * URI
 */
using Uri = std::string;

/**
 * Document URI
 */
using DocumentUri = std::string;

/**
 * Regular expression client capabilities
 */
struct RegularExpressionsClientCapabilities {
  std::string engine;
  std::optional<std::string> version;
};

/**
 * Position in a text document
 */
struct Position {
  int line;
  int character;
};

/**
 * Range in a text document
 */
struct Range {
  Position start;
  Position end;
};

struct TextDocumentItem {
  DocumentUri uri;
  std::string language_id;
  int version;
  std::string text;
};

// JSON conversion functions
void to_json(nlohmann::json& j, const RegularExpressionsClientCapabilities& c);
void from_json(
    const nlohmann::json& j, RegularExpressionsClientCapabilities& c);

void to_json(nlohmann::json& j, const Position& p);
void from_json(const nlohmann::json& j, Position& p);

void to_json(nlohmann::json& j, const Range& r);
void from_json(const nlohmann::json& j, Range& r);

void to_json(nlohmann::json& j, const TextDocumentItem& t);
void from_json(const nlohmann::json& j, TextDocumentItem& t);

}  // namespace lsp

#pragma once

#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace lsp {

/**
 * Symbol kinds as defined by the LSP specification
 */
enum class SymbolKind {
  File = 1,
  Module = 2,
  Namespace = 3,
  Package = 4,
  Class = 5,
  Method = 6,
  Property = 7,
  Field = 8,
  Constructor = 9,
  Enum = 10,
  Interface = 11,
  Function = 12,
  Variable = 13,
  Constant = 14,
  String = 15,
  Number = 16,
  Boolean = 17,
  Array = 18,
  Object = 19,
  Key = 20,
  Null = 21,
  EnumMember = 22,
  Struct = 23,
  Event = 24,
  Operator = 25,
  TypeParameter = 26
};

/**
 * Symbol tags as defined by the LSP specification
 */
enum class SymbolTag { Deprecated = 1 };

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

/**
 * DocumentSymbol as defined by the LSP specification
 * Represents a symbol in a hierarchical structure
 */
struct DocumentSymbol {
  std::string name;                   // Symbol name
  std::optional<std::string> detail;  // Additional details (e.g., signature)
  SymbolKind kind;                    // Symbol kind
  std::optional<std::vector<SymbolTag>> tags;  // Symbol tags (e.g., Deprecated)
  std::optional<bool> deprecated;              // Deprecated flag (legacy)
  Range range;                                 // Full symbol range
  Range selectionRange;                        // Name identifier range
  std::vector<DocumentSymbol> children;        // Child symbols
};

// JSON conversion functions (to be implemented in document_symbol.cpp)
void to_json(nlohmann::json& j, const Position& p);
void from_json(const nlohmann::json& j, Position& p);

void to_json(nlohmann::json& j, const Range& r);
void from_json(const nlohmann::json& j, Range& r);

void to_json(nlohmann::json& j, const SymbolKind& k);
void from_json(const nlohmann::json& j, SymbolKind& k);

void to_json(nlohmann::json& j, const SymbolTag& t);
void from_json(const nlohmann::json& j, SymbolTag& t);

void to_json(nlohmann::json& j, const DocumentSymbol& s);
void from_json(const nlohmann::json& j, DocumentSymbol& s);

}  // namespace lsp

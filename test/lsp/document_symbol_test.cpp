#include "lsp/document_symbol.hpp"

#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

TEST_CASE("Position serialization", "[lsp]") {
  // Create a Position
  lsp::Position pos{.line = 10, .character = 20};

  // Serialize to JSON
  nlohmann::json j;
  to_json(j, pos);

  // Verify JSON structure
  REQUIRE(j.contains("line"));
  REQUIRE(j.contains("character"));
  REQUIRE(j["line"] == 10);
  REQUIRE(j["character"] == 20);

  // Deserialize back to Position
  lsp::Position pos2;
  from_json(j, pos2);

  // Verify deserialized object
  REQUIRE(pos2.line == 10);
  REQUIRE(pos2.character == 20);
}

TEST_CASE("Range serialization", "[lsp]") {
  // Create a Range
  lsp::Range range{
      .start = {.line = 10, .character = 20},
      .end = {.line = 15, .character = 30}};

  // Serialize to JSON
  nlohmann::json j;
  to_json(j, range);

  // Verify JSON structure
  REQUIRE(j.contains("start"));
  REQUIRE(j.contains("end"));
  REQUIRE(j["start"]["line"] == 10);
  REQUIRE(j["start"]["character"] == 20);
  REQUIRE(j["end"]["line"] == 15);
  REQUIRE(j["end"]["character"] == 30);

  // Deserialize back to Range
  lsp::Range range2;
  from_json(j, range2);

  // Verify deserialized object
  REQUIRE(range2.start.line == 10);
  REQUIRE(range2.start.character == 20);
  REQUIRE(range2.end.line == 15);
  REQUIRE(range2.end.character == 30);
}

TEST_CASE("SymbolKind serialization", "[lsp]") {
  // Create a SymbolKind
  lsp::SymbolKind kind = lsp::SymbolKind::Module;

  // Serialize to JSON
  nlohmann::json j;
  to_json(j, kind);

  // Verify JSON structure (should be an integer)
  REQUIRE(j.is_number());
  REQUIRE(j == 2);  // Module is 2 in LSP spec

  // Deserialize back to SymbolKind
  lsp::SymbolKind kind2;
  from_json(j, kind2);

  // Verify deserialized object
  REQUIRE(kind2 == lsp::SymbolKind::Module);
}

TEST_CASE("DocumentSymbol serialization", "[lsp]") {
  // Create a basic DocumentSymbol
  lsp::DocumentSymbol symbol;
  symbol.name = "test_symbol";
  symbol.kind = lsp::SymbolKind::Function;
  symbol.range = {
      .start = {.line = 10, .character = 20},
      .end = {.line = 15, .character = 30}};
  symbol.selectionRange = {
      .start = {.line = 10, .character = 25},
      .end = {.line = 10, .character = 35}};

  // Serialize to JSON
  nlohmann::json j;
  to_json(j, symbol);

  // Verify JSON structure
  REQUIRE(j.contains("name"));
  REQUIRE(j.contains("kind"));
  REQUIRE(j.contains("range"));
  REQUIRE(j.contains("selectionRange"));
  REQUIRE(j.contains("children"));
  REQUIRE(j["name"] == "test_symbol");
  REQUIRE(j["kind"] == 12);  // Function is 12 in LSP spec

  // Deserialize back to DocumentSymbol
  lsp::DocumentSymbol symbol2;
  from_json(j, symbol2);

  // Verify deserialized object
  REQUIRE(symbol2.name == "test_symbol");
  REQUIRE(symbol2.kind == lsp::SymbolKind::Function);
  REQUIRE(symbol2.range.start.line == 10);
  REQUIRE(symbol2.range.end.line == 15);
  REQUIRE(symbol2.children.empty());
}

TEST_CASE("DocumentSymbol with optional fields", "[lsp]") {
  // Create a DocumentSymbol with optional fields
  lsp::DocumentSymbol symbol;
  symbol.name = "test_symbol";
  symbol.kind = lsp::SymbolKind::Variable;
  symbol.range = {
      .start = {.line = 10, .character = 20},
      .end = {.line = 15, .character = 30}};
  symbol.selectionRange = {
      .start = {.line = 10, .character = 25},
      .end = {.line = 10, .character = 35}};

  // Set optional fields
  symbol.detail = "int32_t";
  symbol.deprecated = true;
  symbol.tags = std::vector<lsp::SymbolTag>{lsp::SymbolTag::Deprecated};

  // Serialize to JSON
  nlohmann::json j;
  to_json(j, symbol);

  // Verify JSON structure
  REQUIRE(j.contains("detail"));
  REQUIRE(j.contains("deprecated"));
  REQUIRE(j.contains("tags"));
  REQUIRE(j["detail"] == "int32_t");
  REQUIRE(j["deprecated"] == true);
  REQUIRE(j["tags"].size() == 1);
  REQUIRE(j["tags"][0] == 1);  // Deprecated is 1 in LSP spec

  // Deserialize back to DocumentSymbol
  lsp::DocumentSymbol symbol2;
  from_json(j, symbol2);

  // Verify deserialized object and optional fields
  REQUIRE(symbol2.detail.has_value());
  REQUIRE(symbol2.detail.value() == "int32_t");
  REQUIRE(symbol2.deprecated.has_value());
  REQUIRE(symbol2.deprecated.value() == true);
  REQUIRE(symbol2.tags.has_value());
  REQUIRE(symbol2.tags.value().size() == 1);
  REQUIRE(symbol2.tags.value()[0] == lsp::SymbolTag::Deprecated);
}

TEST_CASE("DocumentSymbol hierarchical structure", "[lsp]") {
  // Create a parent symbol
  lsp::DocumentSymbol parent;
  parent.name = "parent_module";
  parent.kind = lsp::SymbolKind::Module;
  parent.range = {
      .start = {.line = 1, .character = 0},
      .end = {.line = 50, .character = 10}};
  parent.selectionRange = {
      .start = {.line = 1, .character = 7},
      .end = {.line = 1, .character = 20}};

  // Create first child symbol
  lsp::DocumentSymbol child1;
  child1.name = "child_function";
  child1.kind = lsp::SymbolKind::Function;
  child1.range = {
      .start = {.line = 10, .character = 2},
      .end = {.line = 20, .character = 5}};
  child1.selectionRange = {
      .start = {.line = 10, .character = 10},
      .end = {.line = 10, .character = 24}};

  // Create second child symbol
  lsp::DocumentSymbol child2;
  child2.name = "child_variable";
  child2.kind = lsp::SymbolKind::Variable;
  child2.range = {
      .start = {.line = 25, .character = 2},
      .end = {.line = 25, .character = 20}};
  child2.selectionRange = {
      .start = {.line = 25, .character = 8},
      .end = {.line = 25, .character = 21}};

  // Create nested grandchild symbol
  lsp::DocumentSymbol grandchild;
  grandchild.name = "grandchild_struct";
  grandchild.kind = lsp::SymbolKind::Struct;
  grandchild.range = {
      .start = {.line = 15, .character = 4},
      .end = {.line = 18, .character = 5}};
  grandchild.selectionRange = {
      .start = {.line = 15, .character = 10},
      .end = {.line = 15, .character = 25}};

  // Build the hierarchy
  child1.children.push_back(grandchild);
  parent.children.push_back(child1);
  parent.children.push_back(child2);

  // Serialize to JSON
  nlohmann::json j;
  to_json(j, parent);

  // Verify JSON hierarchy structure
  REQUIRE(j["children"].size() == 2);
  REQUIRE(j["children"][0]["name"] == "child_function");
  REQUIRE(j["children"][1]["name"] == "child_variable");
  REQUIRE(j["children"][0]["children"].size() == 1);
  REQUIRE(j["children"][0]["children"][0]["name"] == "grandchild_struct");

  // Deserialize back to DocumentSymbol
  lsp::DocumentSymbol parent2;
  from_json(j, parent2);

  // Verify deserialized hierarchy
  REQUIRE(parent2.children.size() == 2);
  REQUIRE(parent2.children[0].name == "child_function");
  REQUIRE(parent2.children[1].name == "child_variable");
  REQUIRE(parent2.children[0].children.size() == 1);
  REQUIRE(parent2.children[0].children[0].name == "grandchild_struct");
}

TEST_CASE("DocumentSymbol parsing from raw JSON", "[lsp]") {
  // Create a JSON object directly
  std::string json_str = R"({
    "name": "json_symbol",
    "kind": 5,
    "range": {
      "start": { "line": 5, "character": 10 },
      "end": { "line": 10, "character": 20 }
    },
    "selectionRange": {
      "start": { "line": 5, "character": 15 },
      "end": { "line": 5, "character": 25 }
    },
    "children": [
      {
        "name": "json_child",
        "kind": 13,
        "range": {
          "start": { "line": 7, "character": 2 },
          "end": { "line": 7, "character": 15 }
        },
        "selectionRange": {
          "start": { "line": 7, "character": 5 },
          "end": { "line": 7, "character": 15 }
        }
      }
    ]
  })";

  // Parse JSON
  nlohmann::json j = nlohmann::json::parse(json_str);

  // Deserialize to DocumentSymbol
  lsp::DocumentSymbol symbol;
  from_json(j, symbol);

  // Verify the deserialized object
  REQUIRE(symbol.name == "json_symbol");
  REQUIRE(symbol.kind == lsp::SymbolKind::Class);  // 5 is Class
  REQUIRE(symbol.range.start.line == 5);
  REQUIRE(symbol.range.end.line == 10);
  REQUIRE(symbol.children.size() == 1);
  REQUIRE(symbol.children[0].name == "json_child");
  REQUIRE(
      symbol.children[0].kind == lsp::SymbolKind::Variable);  // 13 is Variable
}

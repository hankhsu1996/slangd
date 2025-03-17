#include "lsp/document_symbol.hpp"

namespace lsp {

void to_json(nlohmann::json& j, const Position& p) {
  j = nlohmann::json{{"line", p.line}, {"character", p.character}};
}

void from_json(const nlohmann::json& j, Position& p) {
  j.at("line").get_to(p.line);
  j.at("character").get_to(p.character);
}

void to_json(nlohmann::json& j, const Range& r) {
  j = nlohmann::json{{"start", r.start}, {"end", r.end}};
}

void from_json(const nlohmann::json& j, Range& r) {
  j.at("start").get_to(r.start);
  j.at("end").get_to(r.end);
}

void to_json(nlohmann::json& j, const SymbolKind& k) {
  j = static_cast<int>(k);
}

void from_json(const nlohmann::json& j, SymbolKind& k) {
  k = static_cast<SymbolKind>(j.get<int>());
}

void to_json(nlohmann::json& j, const SymbolTag& t) { j = static_cast<int>(t); }

void from_json(const nlohmann::json& j, SymbolTag& t) {
  t = static_cast<SymbolTag>(j.get<int>());
}

void to_json(nlohmann::json& j, const DocumentSymbol& s) {
  // Required fields
  j = nlohmann::json{
      {"name", s.name},
      {"kind", s.kind},
      {"range", s.range},
      {"selectionRange", s.selectionRange}};

  // Optional fields
  if (s.detail.has_value()) {
    j["detail"] = s.detail.value();
  }

  if (s.tags.has_value()) {
    j["tags"] = s.tags.value();
  }

  if (s.deprecated.has_value()) {
    j["deprecated"] = s.deprecated.value();
  }

  // Children are always serialized, but might be an empty array
  j["children"] = s.children;
}

void from_json(const nlohmann::json& j, DocumentSymbol& s) {
  // Required fields
  j.at("name").get_to(s.name);
  j.at("kind").get_to(s.kind);
  j.at("range").get_to(s.range);
  j.at("selectionRange").get_to(s.selectionRange);

  // Optional fields
  if (j.contains("detail")) {
    s.detail = j.at("detail").get<std::string>();
  }

  if (j.contains("tags")) {
    s.tags = j.at("tags").get<std::vector<SymbolTag>>();
  }

  if (j.contains("deprecated")) {
    s.deprecated = j.at("deprecated").get<bool>();
  }

  // Children field
  if (j.contains("children")) {
    j.at("children").get_to(s.children);
  } else {
    s.children.clear();
  }
}

}  // namespace lsp

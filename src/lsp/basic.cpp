#include "lsp/basic.hpp"

namespace lsp {

void to_json(nlohmann::json& j, const RegularExpressionsClientCapabilities& c) {
  j = nlohmann::json{{"engine", c.engine}};

  if (c.version.has_value()) {
    j["version"] = c.version.value();
  }
}

void from_json(
    const nlohmann::json& j, RegularExpressionsClientCapabilities& c) {
  j.at("engine").get_to(c.engine);

  if (j.contains("version")) {
    c.version = j.at("version").get<std::string>();
  }
}

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

void to_json(nlohmann::json& j, const TextDocumentItem& t) {
  j = nlohmann::json{
      {"uri", t.uri},
      {"languageId", t.language_id},
      {"version", t.version},
      {"text", t.text}};
}

void from_json(const nlohmann::json& j, TextDocumentItem& t) {
  j.at("uri").get_to(t.uri);
  j.at("languageId").get_to(t.language_id);
  j.at("version").get_to(t.version);
  j.at("text").get_to(t.text);
}

}  // namespace lsp

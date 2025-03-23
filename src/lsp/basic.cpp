#include "lsp/basic.hpp"

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

}  // namespace lsp

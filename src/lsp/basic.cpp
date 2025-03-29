#include "lsp/basic.hpp"

namespace lsp {
void to_json(nlohmann::json& j, const CancelParams& c) {
  j = nlohmann::json{{"id", c.id}};
}

void from_json(const nlohmann::json& j, CancelParams& c) {
  j.at("id").get_to(c.id);
}

template <typename T>
void to_json(nlohmann::json& j, const ProgressParams<T>& p) {
  j = nlohmann::json{{"token", p.token}, {"value", p.value}};
}

template <typename T>
void from_json(const nlohmann::json& j, ProgressParams<T>& p) {
  j.at("token").get_to(p.token);
  j.at("value").get_to(p.value);
}

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

void to_json(nlohmann::json& j, const TextDocumentIdentifier& t) {
  j = nlohmann::json{{"uri", t.uri}};
}

void from_json(const nlohmann::json& j, TextDocumentIdentifier& t) {
  j.at("uri").get_to(t.uri);
}

void to_json(nlohmann::json& j, const VersionedTextDocumentIdentifier& v) {
  j = nlohmann::json{{"uri", v.uri}, {"version", v.version}};
}

void from_json(const nlohmann::json& j, VersionedTextDocumentIdentifier& v) {
  j.at("uri").get_to(v.uri);
  j.at("version").get_to(v.version);
}

void to_json(
    nlohmann::json& j, const OptionalVersionedTextDocumentIdentifier& o) {
  j = nlohmann::json{{"uri", o.uri}};
  if (o.version.has_value()) {
    j["version"] = o.version.value();
  }
}

void from_json(
    const nlohmann::json& j, OptionalVersionedTextDocumentIdentifier& o) {
  j.at("uri").get_to(o.uri);
  if (j.contains("version")) {
    o.version = j.at("version").get<int>();
  }
}

void to_json(nlohmann::json& j, const TextDocumentPositionParams& t) {
  j = nlohmann::json{
      {"textDocument", t.textDocument}, {"position", t.position}};
}

void from_json(const nlohmann::json& j, TextDocumentPositionParams& t) {
  j.at("textDocument").get_to(t.textDocument);
  j.at("position").get_to(t.position);
}

void to_json(nlohmann::json& j, const DocumentFilter& d) {
  if (d.language.has_value()) {
    j["language"] = d.language.value();
  }
  if (d.scheme.has_value()) {
    j["scheme"] = d.scheme.value();
  }
  if (d.pattern.has_value()) {
    j["pattern"] = d.pattern.value();
  }
}

void from_json(const nlohmann::json& j, DocumentFilter& d) {
  if (j.contains("language")) {
    d.language = j.at("language").get<std::string>();
  }
  if (j.contains("scheme")) {
    d.scheme = j.at("scheme").get<std::string>();
  }
  if (j.contains("pattern")) {
    d.pattern = j.at("pattern").get<std::string>();
  }
}

void to_json(nlohmann::json& j, const DocumentSelector& d) {
  j = nlohmann::json::array();
  for (const auto& filter : d) {
    j.push_back(filter);
  }
}

void from_json(const nlohmann::json& j, DocumentSelector& d) {
  for (const auto& filter : j) {
    d.push_back(filter.get<DocumentFilter>());
  }
}

void to_json(nlohmann::json& j, const WorkDoneProgressBegin& w) {
  j = nlohmann::json{{"kind", w.kind}, {"title", w.title}};
  if (w.cancellable.has_value()) {
    j["cancellable"] = w.cancellable.value();
  }
  if (w.message.has_value()) {
    j["message"] = w.message.value();
  }
  if (w.percentage.has_value()) {
    j["percentage"] = w.percentage.value();
  }
}

void from_json(const nlohmann::json& j, WorkDoneProgressBegin& w) {
  j.at("kind").get_to(w.kind);
  j.at("title").get_to(w.title);
  if (j.contains("cancellable")) {
    w.cancellable = j.at("cancellable").get<bool>();
  }
  if (j.contains("message")) {
    w.message = j.at("message").get<std::string>();
  }
  if (j.contains("percentage")) {
    w.percentage = j.at("percentage").get<int>();
  }
}

void to_json(nlohmann::json& j, const WorkDoneProgressReport& w) {
  j = nlohmann::json{{"kind", w.kind}};
  if (w.cancellable.has_value()) {
    j["cancellable"] = w.cancellable.value();
  }
  if (w.message.has_value()) {
    j["message"] = w.message.value();
  }
  if (w.percentage.has_value()) {
    j["percentage"] = w.percentage.value();
  }
}

void to_json(nlohmann::json& j, const WorkDoneProgressEnd& w) {
  j = nlohmann::json{{"kind", w.kind}};
  if (w.message.has_value()) {
    j["message"] = w.message.value();
  }
}

void from_json(const nlohmann::json& j, WorkDoneProgressEnd& w) {
  j.at("kind").get_to(w.kind);
  if (j.contains("message")) {
    w.message = j.at("message").get<std::string>();
  }
}

void to_json(nlohmann::json& j, const WorkDoneProgressParams& p) {
  if (p.workDoneToken.has_value()) {
    j["workDoneToken"] = p.workDoneToken.value();
  }
}

void from_json(const nlohmann::json& j, WorkDoneProgressParams& p) {
  if (j.contains("workDoneToken")) {
    p.workDoneToken = j.at("workDoneToken").get<ProgressToken>();
  }
}

void to_json(nlohmann::json& j, const WorkDoneProgressOptions& o) {
  if (o.workDoneProgress.has_value()) {
    j["workDoneProgress"] = o.workDoneProgress.value();
  }
}

void from_json(const nlohmann::json& j, WorkDoneProgressOptions& o) {
  if (j.contains("workDoneProgress")) {
    o.workDoneProgress = j.at("workDoneProgress").get<bool>();
  }
}

void to_json(nlohmann::json& j, const PartialResultParams& p) {
  if (p.partialResultToken.has_value()) {
    j["partialResultToken"] = p.partialResultToken.value();
  }
}

void from_json(const nlohmann::json& j, PartialResultParams& p) {
  if (j.contains("partialResultToken")) {
    p.partialResultToken = j.at("partialResultToken").get<ProgressToken>();
  }
}

void to_json(nlohmann::json& j, const TraceValue& t) {
  switch (t) {
    case TraceValue::Off:
      j = "off";
      break;
    case TraceValue::Messages:
      j = "messages";
      break;
    case TraceValue::Verbose:
      j = "verbose";
      break;
    default:
      throw std::invalid_argument("Invalid trace value");
  }
}

void from_json(const nlohmann::json& j, TraceValue& t) {
  if (j == "off") {
    t = TraceValue::Off;
  } else if (j == "messages") {
    t = TraceValue::Messages;
  } else if (j == "verbose") {
    t = TraceValue::Verbose;
  } else {
    throw std::invalid_argument("Invalid trace value");
  }
}

}  // namespace lsp

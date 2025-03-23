#include "lsp/diagnostic.hpp"

namespace lsp {

void to_json(nlohmann::json& j, const DiagnosticSeverity& s) {
  j = static_cast<int>(s);
}

void from_json(const nlohmann::json& j, DiagnosticSeverity& s) {
  s = static_cast<DiagnosticSeverity>(j.get<int>());
}

void to_json(nlohmann::json& j, const DiagnosticTag& t) {
  j = static_cast<int>(t);
}

void from_json(const nlohmann::json& j, DiagnosticTag& t) {
  t = static_cast<DiagnosticTag>(j.get<int>());
}

void to_json(nlohmann::json& j, const CodeDescription& c) {
  j = nlohmann::json{{"href", c.href}};
}

void from_json(const nlohmann::json& j, CodeDescription& c) {
  j.at("href").get_to(c.href);
}

void to_json(nlohmann::json& j, const DiagnosticRelatedInformation& r) {
  j = nlohmann::json{
      {"location",
       nlohmann::json{{"uri", r.location.uri}, {"range", r.location.range}}},
      {"message", r.message}};
}

void from_json(const nlohmann::json& j, DiagnosticRelatedInformation& r) {
  j.at("location").at("uri").get_to(r.location.uri);
  j.at("location").at("range").get_to(r.location.range);
  j.at("message").get_to(r.message);
}

void to_json(nlohmann::json& j, const Diagnostic& d) {
  // Required fields
  j = nlohmann::json{{"range", d.range}, {"message", d.message}};

  // Optional fields
  if (d.severity.has_value()) {
    j["severity"] = d.severity.value();
  }

  if (d.code.has_value()) {
    j["code"] = d.code.value();
  }

  if (d.codeDescription.has_value()) {
    j["codeDescription"] = d.codeDescription.value();
  }

  if (d.source.has_value()) {
    j["source"] = d.source.value();
  }

  if (d.tags.has_value()) {
    j["tags"] = d.tags.value();
  }

  if (d.relatedInformation.has_value()) {
    j["relatedInformation"] = d.relatedInformation.value();
  }

  if (d.data.has_value()) {
    j["data"] = d.data.value();
  }
}

void from_json(const nlohmann::json& j, Diagnostic& d) {
  // Required fields
  j.at("range").get_to(d.range);
  j.at("message").get_to(d.message);

  // Optional fields
  if (j.contains("severity")) {
    d.severity = j.at("severity").get<DiagnosticSeverity>();
  }

  if (j.contains("code")) {
    if (j.at("code").is_string()) {
      d.code = j.at("code").get<std::string>();
    } else if (j.at("code").is_number()) {
      d.code = std::to_string(j.at("code").get<int>());
    }
  }

  if (j.contains("codeDescription")) {
    d.codeDescription = j.at("codeDescription").get<CodeDescription>();
  }

  if (j.contains("source")) {
    d.source = j.at("source").get<std::string>();
  }

  if (j.contains("tags")) {
    d.tags = j.at("tags").get<std::vector<DiagnosticTag>>();
  }

  if (j.contains("relatedInformation")) {
    d.relatedInformation =
        j.at("relatedInformation")
            .get<std::vector<DiagnosticRelatedInformation>>();
  }

  if (j.contains("data")) {
    d.data = j.at("data");
  }
}

void to_json(nlohmann::json& j, const PublishDiagnosticsParams& p) {
  j = nlohmann::json{{"uri", p.uri}, {"diagnostics", p.diagnostics}};

  if (p.version.has_value()) {
    j["version"] = p.version.value();
  }
}

void from_json(const nlohmann::json& j, PublishDiagnosticsParams& p) {
  j.at("uri").get_to(p.uri);
  j.at("diagnostics").get_to(p.diagnostics);

  if (j.contains("version")) {
    p.version = j.at("version").get<int>();
  }
}

}  // namespace lsp

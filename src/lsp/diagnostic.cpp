#include "lsp/diagnostic.hpp"

#include "lsp/json_utils.hpp"

namespace lsp {

// PublishDiagnostics Notification
void to_json(nlohmann::json& j, const PublishDiagnosticsParams& p) {
  to_json_required(j, "uri", p.uri);
  to_json_optional(j, "version", p.version);
  to_json_required(j, "diagnostics", p.diagnostics);
}

void from_json(const nlohmann::json& j, PublishDiagnosticsParams& p) {
  from_json_required(j, "uri", p.uri);
  from_json_optional(j, "version", p.version);
  from_json_required(j, "diagnostics", p.diagnostics);
}

// Pull Diagnostics
void to_json(nlohmann::json& j, const DocumentDiagnosticParams& p) {
  to_json_required(j, "textDocument", p.textDocument);
  to_json_optional(j, "identifier", p.identifier);
  to_json_optional(j, "previousResultId", p.previousResultId);
}

void from_json(const nlohmann::json& j, DocumentDiagnosticParams& p) {
  from_json_required(j, "textDocument", p.textDocument);
  from_json_optional(j, "identifier", p.identifier);
  from_json_optional(j, "previousResultId", p.previousResultId);
}

void to_json(nlohmann::json& j, const DocumentDiagnosticReportKind& k) {
  // 'full', 'unchanged'
  switch (k) {
    case DocumentDiagnosticReportKind::kFull:
      j = "full";
      break;
    case DocumentDiagnosticReportKind::kUnchanged:
      j = "unchanged";
      break;
    default:
      throw std::runtime_error("Invalid document diagnostic report kind");
  }
}

void from_json(const nlohmann::json& j, DocumentDiagnosticReportKind& k) {
  if (j == "full") {
    k = DocumentDiagnosticReportKind::kFull;
  } else if (j == "unchanged") {
    k = DocumentDiagnosticReportKind::kUnchanged;
  } else {
    throw std::runtime_error("Invalid document diagnostic report kind");
  }
}

void to_json(nlohmann::json& j, const FullDocumentDiagnosticReport& r) {
  to_json_required(j, "kind", r.kind);
  to_json_optional(j, "resultId", r.resultId);
  to_json_required(j, "items", r.items);
}

void from_json(const nlohmann::json& j, FullDocumentDiagnosticReport& r) {
  from_json_required(j, "kind", r.kind);
  from_json_optional(j, "resultId", r.resultId);
  from_json_required(j, "items", r.items);
}

void to_json(nlohmann::json& j, const UnchangedDocumentDiagnosticReport& r) {
  to_json_required(j, "kind", r.kind);
  to_json_required(j, "resultId", r.resultId);
}

void from_json(const nlohmann::json& j, UnchangedDocumentDiagnosticReport& r) {
  from_json_required(j, "kind", r.kind);
  from_json_required(j, "resultId", r.resultId);
}

void to_json(nlohmann::json& j, const RelatedFullDocumentDiagnosticReport& r) {
  to_json_required(j, "kind", r.kind);
  to_json_optional(j, "resultId", r.resultId);
  to_json_required(j, "items", r.items);
  // TODO: relatedDocuments
}

void from_json(
    const nlohmann::json& j, RelatedFullDocumentDiagnosticReport& r) {
  from_json_required(j, "kind", r.kind);
  from_json_optional(j, "resultId", r.resultId);
  from_json_required(j, "items", r.items);
  // TODO: relatedDocuments
}

void to_json(
    nlohmann::json& j, const RelatedUnchangedDocumentDiagnosticReport& r) {
  to_json_required(j, "kind", r.kind);
  to_json_required(j, "resultId", r.resultId);
  // TODO: relatedDocuments
}

void from_json(
    const nlohmann::json& j, RelatedUnchangedDocumentDiagnosticReport& r) {
  from_json_required(j, "kind", r.kind);
  from_json_required(j, "resultId", r.resultId);
  // TODO: relatedDocuments
}

void to_json(nlohmann::json& j, const DocumentDiagnosticReport& r) {
  std::visit([&j](auto&& arg) { to_json(j, arg); }, r);
}

void from_json(const nlohmann::json& j, DocumentDiagnosticReport& r) {
  if (j.at("kind") == "full") {
    r = j.get<RelatedFullDocumentDiagnosticReport>();
  } else if (j.at("kind") == "unchanged") {
    r = j.get<RelatedUnchangedDocumentDiagnosticReport>();
  } else {
    throw std::runtime_error("Invalid document diagnostic report kind");
  }
}

void to_json(nlohmann::json& j, const DiagnosticServerCancellationData& d) {
  to_json_required(j, "retriggerRequest", d.retriggerRequest);
}

void from_json(const nlohmann::json& j, DiagnosticServerCancellationData& d) {
  from_json_required(j, "retriggerRequest", d.retriggerRequest);
}

// Workspace Diagnostics
void to_json(nlohmann::json& j, const PreviousResultId& p) {
  to_json_required(j, "uri", p.uri);
  to_json_required(j, "value", p.value);
}

void from_json(const nlohmann::json& j, PreviousResultId& p) {
  from_json_required(j, "uri", p.uri);
  from_json_required(j, "value", p.value);
}

void to_json(nlohmann::json& j, const WorkspaceDiagnosticParams& p) {
  to_json_optional(j, "identifier", p.identifier);
  to_json_required(j, "previousResultIds", p.previousResultIds);
}

void from_json(const nlohmann::json& j, WorkspaceDiagnosticParams& p) {
  from_json_optional(j, "identifier", p.identifier);
  from_json_required(j, "previousResultIds", p.previousResultIds);
}

void to_json(
    nlohmann::json& j, const WorkspaceFullDocumentDiagnosticReport& r) {
  to_json_required(j, "uri", r.uri);
  to_json_optional(j, "version", r.version);
}

void from_json(
    const nlohmann::json& j, WorkspaceFullDocumentDiagnosticReport& r) {
  from_json_required(j, "uri", r.uri);
  from_json_optional(j, "version", r.version);
}

void to_json(
    nlohmann::json& j, const WorkspaceUnchangedDocumentDiagnosticReport& r) {
  to_json_required(j, "uri", r.uri);
  to_json_optional(j, "version", r.version);
}

void from_json(
    const nlohmann::json& j, WorkspaceUnchangedDocumentDiagnosticReport& r) {
  from_json_required(j, "uri", r.uri);
  from_json_optional(j, "version", r.version);
}

void to_json(nlohmann::json& j, const WorkspaceDocumentDiagnosticReport& r) {
  std::visit([&j](auto&& arg) { to_json(j, arg); }, r);
}

void from_json(const nlohmann::json& j, WorkspaceDocumentDiagnosticReport& r) {
  if (j.at("kind") == "full") {
    r = j.get<WorkspaceFullDocumentDiagnosticReport>();
  } else if (j.at("kind") == "unchanged") {
    r = j.get<WorkspaceUnchangedDocumentDiagnosticReport>();
  } else {
    throw std::runtime_error(
        "Invalid workspace document diagnostic report kind");
  }
}

void to_json(nlohmann::json& j, const WorkspaceDiagnosticReport& r) {
  to_json_required(j, "items", r.items);
}

void from_json(const nlohmann::json& j, WorkspaceDiagnosticReport& r) {
  from_json_required(j, "items", r.items);
}

// Diagnostics Refresh
void to_json(nlohmann::json&, const DiagnosticRefreshParams&) {};
void from_json(const nlohmann::json&, DiagnosticRefreshParams&) {};
void to_json(nlohmann::json&, const DiagnosticRefreshResult&) {};
void from_json(const nlohmann::json&, DiagnosticRefreshResult&) {};

}  // namespace lsp

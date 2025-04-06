#pragma once

#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "lsp/basic.hpp"

namespace lsp {

// PublishDiagnostics Notification
struct PublishDiagnosticsParams {
  DocumentUri uri;
  std::optional<int> version;
  std::vector<Diagnostic> diagnostics;
};

void to_json(nlohmann::json& j, const PublishDiagnosticsParams& p);
void from_json(const nlohmann::json& j, PublishDiagnosticsParams& p);

// Pull Diagnostics
struct DocumentDiagnosticParams : WorkDoneProgressParams, PartialResultParams {
  TextDocumentIdentifier textDocument;
  std::optional<std::string> identifier;
  std::optional<std::string> previousResultId;
};

void to_json(nlohmann::json& j, const DocumentDiagnosticParams& p);
void from_json(const nlohmann::json& j, DocumentDiagnosticParams& p);

enum class DocumentDiagnosticReportKind {
  kFull,
  kUnchanged,
};

void to_json(nlohmann::json& j, const DocumentDiagnosticReportKind& k);
void from_json(const nlohmann::json& j, DocumentDiagnosticReportKind& k);

struct FullDocumentDiagnosticReport {
  DocumentDiagnosticReportKind kind = DocumentDiagnosticReportKind::kFull;
  std::optional<std::string> resultId;
  std::vector<Diagnostic> items;
};

void to_json(nlohmann::json& j, const FullDocumentDiagnosticReport& r);
void from_json(const nlohmann::json& j, FullDocumentDiagnosticReport& r);

struct UnchangedDocumentDiagnosticReport {
  DocumentDiagnosticReportKind kind = DocumentDiagnosticReportKind::kUnchanged;
  std::string resultId;
};

void to_json(nlohmann::json& j, const UnchangedDocumentDiagnosticReport& r);
void from_json(const nlohmann::json& j, UnchangedDocumentDiagnosticReport& r);

struct RelatedFullDocumentDiagnosticReport : FullDocumentDiagnosticReport {
  std::optional<std::map<
      std::string,
      std::variant<
          FullDocumentDiagnosticReport, UnchangedDocumentDiagnosticReport>>>
      relatedDocuments;
};

void to_json(nlohmann::json& j, const RelatedFullDocumentDiagnosticReport& r);
void from_json(const nlohmann::json& j, RelatedFullDocumentDiagnosticReport& r);

struct RelatedUnchangedDocumentDiagnosticReport
    : UnchangedDocumentDiagnosticReport {
  std::optional<std::map<
      std::string,
      std::variant<
          FullDocumentDiagnosticReport, UnchangedDocumentDiagnosticReport>>>
      relatedDocuments;
};

void to_json(
    nlohmann::json& j, const RelatedUnchangedDocumentDiagnosticReport& r);
void from_json(
    const nlohmann::json& j, RelatedUnchangedDocumentDiagnosticReport& r);

using DocumentDiagnosticReport = std::variant<
    RelatedFullDocumentDiagnosticReport,
    RelatedUnchangedDocumentDiagnosticReport>;

void to_json(nlohmann::json& j, const DocumentDiagnosticReport& r);
void from_json(const nlohmann::json& j, DocumentDiagnosticReport& r);

struct DiagnosticServerCancellationData {
  bool retriggerRequest;
};

void to_json(nlohmann::json& j, const DiagnosticServerCancellationData& d);
void from_json(const nlohmann::json& j, DiagnosticServerCancellationData& d);

// Workspace Diagnostics
struct PreviousResultId {
  DocumentUri uri;
  std::string value;
};

void to_json(nlohmann::json& j, const PreviousResultId& p);
void from_json(const nlohmann::json& j, PreviousResultId& p);

struct WorkspaceDiagnosticParams : WorkDoneProgressParams, PartialResultParams {
  std::optional<std::string> identifier;
  std::vector<PreviousResultId> previousResultIds;
};

void to_json(nlohmann::json& j, const WorkspaceDiagnosticParams& p);
void from_json(const nlohmann::json& j, WorkspaceDiagnosticParams& p);

struct WorkspaceFullDocumentDiagnosticReport : FullDocumentDiagnosticReport {
  DocumentUri uri;
  std::optional<int> version;
};

void to_json(nlohmann::json& j, const WorkspaceFullDocumentDiagnosticReport& r);
void from_json(
    const nlohmann::json& j, WorkspaceFullDocumentDiagnosticReport& r);

struct WorkspaceUnchangedDocumentDiagnosticReport
    : UnchangedDocumentDiagnosticReport {
  DocumentUri uri;
  std::optional<int> version;
};

void to_json(
    nlohmann::json& j, const WorkspaceUnchangedDocumentDiagnosticReport& r);
void from_json(
    const nlohmann::json& j, WorkspaceUnchangedDocumentDiagnosticReport& r);

using WorkspaceDocumentDiagnosticReport = std::variant<
    WorkspaceFullDocumentDiagnosticReport,
    WorkspaceUnchangedDocumentDiagnosticReport>;

void to_json(nlohmann::json& j, const WorkspaceDocumentDiagnosticReport& r);
void from_json(const nlohmann::json& j, WorkspaceDocumentDiagnosticReport& r);

struct WorkspaceDiagnosticReport {
  std::vector<WorkspaceDocumentDiagnosticReport> items;
};

void to_json(nlohmann::json& j, const WorkspaceDiagnosticReport& r);
void from_json(const nlohmann::json& j, WorkspaceDiagnosticReport& r);

// Diagnostics Refresh
struct DiagnosticRefreshParams {};

void to_json(nlohmann::json& j, const DiagnosticRefreshParams& p);
void from_json(const nlohmann::json& j, DiagnosticRefreshParams& p);

struct DiagnosticRefreshResult {};

void to_json(nlohmann::json& j, const DiagnosticRefreshResult& r);
void from_json(const nlohmann::json& j, DiagnosticRefreshResult& r);

}  // namespace lsp

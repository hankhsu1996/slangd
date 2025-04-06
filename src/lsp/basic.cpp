#include "lsp/basic.hpp"

#include "lsp/json_utils.hpp"

namespace lsp {

// Cancelation support
void to_json(nlohmann::json& j, const CancelParams& p) {
  j = nlohmann::json{{"id", p.id}};
}

void from_json(const nlohmann::json& j, CancelParams& p) {
  j.at("id").get_to(p.id);
}

// Progress support
template <typename T>
void to_json(nlohmann::json& j, const ProgressParams<T>& p) {
  j = nlohmann::json{{"token", p.token}, {"value", p.value}};
}

template <typename T>
void from_json(const nlohmann::json& j, ProgressParams<T>& p) {
  j.at("token").get_to(p.token);
  j.at("value").get_to(p.value);
}

// Regular Expressions
void to_json(nlohmann::json& j, const RegularExpressionsClientCapabilities& c) {
  j = nlohmann::json{{"engine", c.engine}};
  to_json_optional(j, "version", c.version);
}

void from_json(
    const nlohmann::json& j, RegularExpressionsClientCapabilities& c) {
  j.at("engine").get_to(c.engine);
  from_json_optional(j, "version", c.version);
}

// Position
void to_json(nlohmann::json& j, const Position& p) {
  j = nlohmann::json{{"line", p.line}, {"character", p.character}};
}

void from_json(const nlohmann::json& j, Position& p) {
  j.at("line").get_to(p.line);
  j.at("character").get_to(p.character);
}

void to_json(nlohmann::json& j, const PositionEncodingKind& p) {
  switch (p) {
    case PositionEncodingKind::kUtf8:
      j = "utf-8";
      break;
    case PositionEncodingKind::kUtf16:
      j = "utf-16";
      break;
    case PositionEncodingKind::kUtf32:
      j = "utf-32";
      break;
    default:
      throw std::runtime_error("Invalid position encoding kind");
  }
}

void from_json(const nlohmann::json& j, PositionEncodingKind& p) {
  std::string s = j.get<std::string>();
  if (s == "utf-8") {
    p = PositionEncodingKind::kUtf8;
  } else if (s == "utf-16") {
    p = PositionEncodingKind::kUtf16;
  } else if (s == "utf-32") {
    p = PositionEncodingKind::kUtf32;
  } else {
    throw std::runtime_error("Invalid position encoding kind");
  }
}

// Range
void to_json(nlohmann::json& j, const Range& r) {
  j = nlohmann::json{{"start", r.start}, {"end", r.end}};
}

void from_json(const nlohmann::json& j, Range& r) {
  j.at("start").get_to(r.start);
  j.at("end").get_to(r.end);
}

// Text Document Item
void to_json(nlohmann::json& j, const TextDocumentItem& t) {
  j = nlohmann::json{
      {"uri", t.uri},
      {"languageId", t.languageId},
      {"version", t.version},
      {"text", t.text}};
}

void from_json(const nlohmann::json& j, TextDocumentItem& t) {
  j.at("uri").get_to(t.uri);
  j.at("languageId").get_to(t.languageId);
  j.at("version").get_to(t.version);
  j.at("text").get_to(t.text);
}

// Text Document Identifier
void to_json(nlohmann::json& j, const TextDocumentIdentifier& t) {
  j = nlohmann::json{{"uri", t.uri}};
}

void from_json(const nlohmann::json& j, TextDocumentIdentifier& t) {
  j.at("uri").get_to(t.uri);
}

// Versioned Text Document Identifier
void to_json(nlohmann::json& j, const VersionedTextDocumentIdentifier& v) {
  j = nlohmann::json{{"uri", v.uri}, {"version", v.version}};
}

void from_json(const nlohmann::json& j, VersionedTextDocumentIdentifier& v) {
  j.at("uri").get_to(v.uri);
  j.at("version").get_to(v.version);
}

// Optional Versioned Text Document Identifier
void to_json(
    nlohmann::json& j, const OptionalVersionedTextDocumentIdentifier& o) {
  j = nlohmann::json{{"uri", o.uri}};
  to_json_optional(j, "version", o.version);
}

void from_json(
    const nlohmann::json& j, OptionalVersionedTextDocumentIdentifier& o) {
  j.at("uri").get_to(o.uri);
  from_json_optional(j, "version", o.version);
}

// Text Document Position Params
void to_json(nlohmann::json& j, const TextDocumentPositionParams& t) {
  j = nlohmann::json{
      {"textDocument", t.textDocument}, {"position", t.position}};
}

void from_json(const nlohmann::json& j, TextDocumentPositionParams& t) {
  j.at("textDocument").get_to(t.textDocument);
  j.at("position").get_to(t.position);
}

// Document Filter
void to_json(nlohmann::json& j, const DocumentFilter& d) {
  to_json_optional(j, "language", d.language);
  to_json_optional(j, "scheme", d.scheme);
  to_json_optional(j, "pattern", d.pattern);
}

void from_json(const nlohmann::json& j, DocumentFilter& d) {
  from_json_optional(j, "language", d.language);
  from_json_optional(j, "scheme", d.scheme);
  from_json_optional(j, "pattern", d.pattern);
}

// Document Selector
void to_json(nlohmann::json& j, const DocumentSelector& d) {
  j = nlohmann::json::array();
  for (const auto& filter : d) {
    j.push_back(filter);
  }
}

void from_json(const nlohmann::json& j, DocumentSelector& d) {
  d.clear();
  for (const auto& filter : j) {
    d.push_back(filter.get<DocumentFilter>());
  }
}

// Text Edit
void to_json(nlohmann::json& j, const TextEdit& t) {
  j = nlohmann::json{{"range", t.range}, {"newText", t.newText}};
}

void from_json(const nlohmann::json& j, TextEdit& t) {
  j.at("range").get_to(t.range);
  j.at("newText").get_to(t.newText);
}

// Change Annotation
void to_json(nlohmann::json& j, const ChangeAnnotation& c) {
  j = nlohmann::json{{"label", c.label}};
  to_json_optional(j, "needsConfirmation", c.needsConfirmation);
  to_json_optional(j, "description", c.description);
}

void from_json(const nlohmann::json& j, ChangeAnnotation& c) {
  j.at("label").get_to(c.label);
  from_json_optional(j, "needsConfirmation", c.needsConfirmation);
  from_json_optional(j, "description", c.description);
}

void to_json(nlohmann::json& j, const AnnotatedTextEdit& a) {
  j = nlohmann::json{
      {"range", a.range},
      {"newText", a.newText},
      {"annotationId", a.annotationId}};
}

void from_json(const nlohmann::json& j, AnnotatedTextEdit& a) {
  j.at("range").get_to(a.range);
  j.at("newText").get_to(a.newText);
  j.at("annotationId").get_to(a.annotationId);
}

// Text Document Edit
void to_json(nlohmann::json& j, const TextEditVariant& t) {
  std::visit([&j](auto&& arg) { j = arg; }, t);
}

void from_json(const nlohmann::json& j, TextEditVariant& t) {
  if (j.contains("annotationId")) {
    t = j.get<AnnotatedTextEdit>();
  } else {
    t = j.get<TextEdit>();
  }
}

void to_json(nlohmann::json& j, const TextDocumentEdit& t) {
  j = nlohmann::json{{"edits", t.edits}};
  to_json_optional(j, "textDocument", t.textDocument);
}

void from_json(const nlohmann::json& j, TextDocumentEdit& t) {
  j.at("edits").get_to(t.edits);
  from_json_optional(j, "textDocument", t.textDocument);
}

// Location
void to_json(nlohmann::json& j, const Location& l) {
  j = nlohmann::json{{"uri", l.uri}, {"range", l.range}};
}

void from_json(const nlohmann::json& j, Location& l) {
  j.at("uri").get_to(l.uri);
  j.at("range").get_to(l.range);
}

// Location Link
void to_json(nlohmann::json& j, const LocationLink& l) {
  j = nlohmann::json{
      {"targetUri", l.targetUri},
      {"targetRange", l.targetRange},
      {"targetSelectionRange", l.targetSelectionRange}};
  to_json_optional(j, "originSelectionRange", l.originSelectionRange);
}

void from_json(const nlohmann::json& j, LocationLink& l) {
  j.at("targetUri").get_to(l.targetUri);
  j.at("targetRange").get_to(l.targetRange);
  j.at("targetSelectionRange").get_to(l.targetSelectionRange);
  from_json_optional(j, "originSelectionRange", l.originSelectionRange);
}

// Diagnostic
void to_json(nlohmann::json& j, const DiagnosticSeverity& d) {
  j = static_cast<int>(d);
}

void from_json(const nlohmann::json& j, DiagnosticSeverity& d) {
  d = static_cast<DiagnosticSeverity>(j.get<int>());
}

void to_json(nlohmann::json& j, const DiagnosticTag& d) {
  j = static_cast<int>(d);
}

void from_json(const nlohmann::json& j, DiagnosticTag& d) {
  d = static_cast<DiagnosticTag>(j.get<int>());
}

void to_json(nlohmann::json& j, const CodeDescription& c) {
  j = nlohmann::json{{"href", c.href}};
}

void from_json(const nlohmann::json& j, CodeDescription& c) {
  j.at("href").get_to(c.href);
}

void to_json(nlohmann::json& j, const DiagnosticRelatedInformation& r) {
  j = nlohmann::json{{"location", r.location}, {"message", r.message}};
}

void from_json(const nlohmann::json& j, DiagnosticRelatedInformation& r) {
  j.at("location").get_to(r.location);
  j.at("message").get_to(r.message);
}

void to_json(nlohmann::json& j, const Diagnostic& d) {
  j = nlohmann::json{};
  j["range"] = d.range;
  to_json_optional(j, "severity", d.severity);
  to_json_optional(j, "code", d.code);
  to_json_optional(j, "codeDescription", d.codeDescription);
  to_json_optional(j, "source", d.source);
  j["message"] = d.message;
  to_json_optional(j, "tags", d.tags);
  to_json_optional(j, "relatedInformation", d.relatedInformation);
  to_json_optional(j, "data", d.data);
}

void from_json(const nlohmann::json& j, Diagnostic& d) {
  j.at("range").get_to(d.range);
  from_json_optional(j, "severity", d.severity);
  from_json_optional(j, "code", d.code);
  from_json_optional(j, "codeDescription", d.codeDescription);
  from_json_optional(j, "source", d.source);
  j.at("message").get_to(d.message);
  from_json_optional(j, "tags", d.tags);
  from_json_optional(j, "relatedInformation", d.relatedInformation);
  from_json_optional(j, "data", d.data);
}

// Command
void to_json(nlohmann::json& j, const Command& c) {
  j = nlohmann::json{{"title", c.title}, {"command", c.command}};
  to_json_optional(j, "arguments", c.arguments);
}

void from_json(const nlohmann::json& j, Command& c) {
  j.at("title").get_to(c.title);
  j.at("command").get_to(c.command);
  from_json_optional(j, "arguments", c.arguments);
}

// Markup Content
void to_json(nlohmann::json& j, const MarkupKind& m) {
  switch (m) {
    case MarkupKind::kPlainText:
      j = "plaintext";
      break;
    case MarkupKind::kMarkdown:
      j = "markdown";
      break;
    default:
      throw std::runtime_error("Invalid markup kind");
  }
}

void from_json(const nlohmann::json& j, MarkupKind& m) {
  std::string s = j.get<std::string>();
  if (s == "plaintext") {
    m = MarkupKind::kPlainText;
  } else if (s == "markdown") {
    m = MarkupKind::kMarkdown;
  } else {
    throw std::runtime_error("Invalid markup kind");
  }
}

void to_json(nlohmann::json& j, const MarkupContent& m) {
  j = nlohmann::json{{"kind", m.kind}, {"value", m.value}};
}

void from_json(const nlohmann::json& j, MarkupContent& m) {
  j.at("kind").get_to(m.kind);
  j.at("value").get_to(m.value);
}

// Markdown Client Capabilities
void to_json(nlohmann::json& j, const MarkdownClientCapabilities& m) {
  j = nlohmann::json{{"parser", m.parser}};
  to_json_optional(j, "version", m.version);
  to_json_optional(j, "allowedTags", m.allowedTags);
}

void from_json(const nlohmann::json& j, MarkdownClientCapabilities& m) {
  j.at("parser").get_to(m.parser);
  from_json_optional(j, "version", m.version);
  from_json_optional(j, "allowedTags", m.allowedTags);
}

// File Resource changes
void to_json(nlohmann::json& j, const CreateFileOptions& c) {
  j = nlohmann::json{};
  to_json_optional(j, "overwrite", c.overwrite);
  to_json_optional(j, "ignoreIfExists", c.ignoreIfExists);
}

void from_json(const nlohmann::json& j, CreateFileOptions& c) {
  from_json_optional(j, "overwrite", c.overwrite);
  from_json_optional(j, "ignoreIfExists", c.ignoreIfExists);
}

void to_json(nlohmann::json& j, const CreateFile& c) {
  j = nlohmann::json{{"kind", c.kind}, {"uri", c.uri}};
  to_json_optional(j, "options", c.options);
  to_json_optional(j, "annotationId", c.annotationId);
}

void from_json(const nlohmann::json& j, CreateFile& c) {
  j.at("kind").get_to(c.kind);
  j.at("uri").get_to(c.uri);
  from_json_optional(j, "options", c.options);
  from_json_optional(j, "annotationId", c.annotationId);
}

void to_json(nlohmann::json& j, const RenameFileOptions& r) {
  j = nlohmann::json{};
  to_json_optional(j, "overwrite", r.overwrite);
  to_json_optional(j, "ignoreIfExists", r.ignoreIfExists);
}

void from_json(const nlohmann::json& j, RenameFileOptions& r) {
  from_json_optional(j, "overwrite", r.overwrite);
  from_json_optional(j, "ignoreIfExists", r.ignoreIfExists);
}

void to_json(nlohmann::json& j, const RenameFile& r) {
  j = nlohmann::json{
      {"kind", r.kind}, {"oldUri", r.oldUri}, {"newUri", r.newUri}};
  to_json_optional(j, "options", r.options);
  to_json_optional(j, "annotationId", r.annotationId);
}

void from_json(const nlohmann::json& j, RenameFile& r) {
  j.at("kind").get_to(r.kind);
  j.at("oldUri").get_to(r.oldUri);
  j.at("newUri").get_to(r.newUri);
  from_json_optional(j, "options", r.options);
  from_json_optional(j, "annotationId", r.annotationId);
}

void to_json(nlohmann::json& j, const DeleteFileOptions& d) {
  j = nlohmann::json{};
  to_json_optional(j, "recursive", d.recursive);
  to_json_optional(j, "ignoreIfNotExists", d.ignoreIfNotExists);
}

void from_json(const nlohmann::json& j, DeleteFileOptions& d) {
  from_json_optional(j, "recursive", d.recursive);
  from_json_optional(j, "ignoreIfNotExists", d.ignoreIfNotExists);
}

void to_json(nlohmann::json& j, const DeleteFile& d) {
  j = nlohmann::json{{"kind", d.kind}, {"uri", d.uri}};
  to_json_optional(j, "options", d.options);
  to_json_optional(j, "annotationId", d.annotationId);
}

void from_json(const nlohmann::json& j, DeleteFile& d) {
  j.at("kind").get_to(d.kind);
  j.at("uri").get_to(d.uri);
  from_json_optional(j, "options", d.options);
  from_json_optional(j, "annotationId", d.annotationId);
}

void to_json(nlohmann::json& j, const ChangeAnnotations& c) {
  j = nlohmann::json::object();
  for (const auto& [key, value] : c) {
    j[key] = value;
  }
}

void from_json(const nlohmann::json& j, ChangeAnnotations& c) {
  for (const auto& [key, value] : j.items()) {
    c[key] = value.get<ChangeAnnotation>();
  }
}

void to_json(nlohmann::json& j, const DocumentChange& d) {
  std::visit([&j](auto&& arg) { j = arg; }, d);
}

void from_json(const nlohmann::json& j, DocumentChange& d) {
  // we can use the 'kind' field to determine the type of the document change
  if (j.at("kind").get<std::string>() == "create") {
    d = j.get<CreateFile>();
  } else if (j.at("kind").get<std::string>() == "rename") {
    d = j.get<RenameFile>();
  } else if (j.at("kind").get<std::string>() == "delete") {
    d = j.get<DeleteFile>();
  } else {
    throw std::runtime_error("Invalid document change kind");
  }
}

void to_json(nlohmann::json& j, const WorkspaceEdit& w) {
  j = nlohmann::json{};
  to_json_optional(j, "changes", w.changes);
  to_json_optional(j, "documentChanges", w.documentChanges);
  to_json_optional(j, "changeAnnotations", w.changeAnnotations);
}

void from_json(const nlohmann::json& j, WorkspaceEdit& w) {
  from_json_optional(j, "changes", w.changes);
  from_json_optional(j, "documentChanges", w.documentChanges);
  from_json_optional(j, "changeAnnotations", w.changeAnnotations);
}

// Workspace Edit Client Capabilities
void to_json(nlohmann::json& j, const ResourceOperationKind& r) {
  switch (r) {
    case ResourceOperationKind::kCreate:
      j = "create";
      break;
    case ResourceOperationKind::kRename:
      j = "rename";
      break;
    case ResourceOperationKind::kDelete:
      j = "delete";
      break;
    default:
      throw std::runtime_error("Invalid resource operation kind");
  }
}

void from_json(const nlohmann::json& j, ResourceOperationKind& r) {
  std::string s = j.get<std::string>();
  if (s == "create") {
    r = ResourceOperationKind::kCreate;
  } else if (s == "rename") {
    r = ResourceOperationKind::kRename;
  } else if (s == "delete") {
    r = ResourceOperationKind::kDelete;
  } else {
    throw std::runtime_error("Invalid resource operation kind");
  }
}

void to_json(nlohmann::json& j, const FailureHandlingKind& f) {
  switch (f) {
    case FailureHandlingKind::kAbort:
      j = "abort";
      break;
    case FailureHandlingKind::kTransactional:
      j = "transactional";
      break;
    case FailureHandlingKind::kUndo:
      j = "undo";
      break;
    case FailureHandlingKind::kTextOnlyTransactional:
      j = "textOnlyTransactional";
      break;
    default:
      throw std::runtime_error("Invalid failure handling kind");
  }
}

void from_json(const nlohmann::json& j, FailureHandlingKind& f) {
  std::string s = j.get<std::string>();
  if (s == "abort") {
    f = FailureHandlingKind::kAbort;
  } else if (s == "transactional") {
    f = FailureHandlingKind::kTransactional;
  } else if (s == "undo") {
    f = FailureHandlingKind::kUndo;
  } else if (s == "textOnlyTransactional") {
    f = FailureHandlingKind::kTextOnlyTransactional;
  } else {
    throw std::runtime_error("Invalid failure handling kind");
  }
}

void to_json(nlohmann::json& j, const ChangeAnnotationSupport& c) {
  j = nlohmann::json{};
  to_json_optional(j, "groupsOnLabel", c.groupsOnLabel);
}

void from_json(const nlohmann::json& j, ChangeAnnotationSupport& c) {
  from_json_optional(j, "groupsOnLabel", c.groupsOnLabel);
}

void to_json(nlohmann::json& j, const WorkspaceEditClientCapabilities& w) {
  j = nlohmann::json{};
  to_json_optional(j, "documentChanges", w.documentChanges);
  to_json_optional(j, "resourceOperations", w.resourceOperations);
  to_json_optional(j, "failureHandling", w.failureHandling);
  to_json_optional(j, "normalizesLineEndings", w.normalizesLineEndings);
  to_json_optional(j, "changeAnnotationSupport", w.changeAnnotationSupport);
}

void from_json(const nlohmann::json& j, WorkspaceEditClientCapabilities& w) {
  from_json_optional(j, "documentChanges", w.documentChanges);
  from_json_optional(j, "resourceOperations", w.resourceOperations);
  from_json_optional(j, "failureHandling", w.failureHandling);
  from_json_optional(j, "normalizesLineEndings", w.normalizesLineEndings);
  from_json_optional(j, "changeAnnotationSupport", w.changeAnnotationSupport);
}

// Work Done Progress
void to_json(nlohmann::json& j, const WorkDoneProgressBegin& w) {
  j = nlohmann::json{{"kind", w.kind}, {"title", w.title}};
  to_json_optional(j, "cancellable", w.cancellable);
  to_json_optional(j, "message", w.message);
  to_json_optional(j, "percentage", w.percentage);
}

void from_json(const nlohmann::json& j, WorkDoneProgressBegin& w) {
  j.at("kind").get_to(w.kind);
  j.at("title").get_to(w.title);
  from_json_optional(j, "cancellable", w.cancellable);
  from_json_optional(j, "message", w.message);
  from_json_optional(j, "percentage", w.percentage);
}

void to_json(nlohmann::json& j, const WorkDoneProgressReport& w) {
  j = nlohmann::json{{"kind", w.kind}};
  to_json_optional(j, "cancellable", w.cancellable);
  to_json_optional(j, "message", w.message);
  to_json_optional(j, "percentage", w.percentage);
}

void from_json(const nlohmann::json& j, WorkDoneProgressReport& w) {
  j.at("kind").get_to(w.kind);
  from_json_optional(j, "cancellable", w.cancellable);
  from_json_optional(j, "message", w.message);
  from_json_optional(j, "percentage", w.percentage);
}

void to_json(nlohmann::json& j, const WorkDoneProgressEnd& w) {
  j = nlohmann::json{{"kind", w.kind}};
  to_json_optional(j, "message", w.message);
}

void from_json(const nlohmann::json& j, WorkDoneProgressEnd& w) {
  j.at("kind").get_to(w.kind);
  from_json_optional(j, "message", w.message);
}

void to_json(nlohmann::json& j, const WorkDoneProgressParams& p) {
  j = nlohmann::json{};
  to_json_optional(j, "workDoneToken", p.workDoneToken);
}

void from_json(const nlohmann::json& j, WorkDoneProgressParams& p) {
  from_json_optional(j, "workDoneToken", p.workDoneToken);
}

void to_json(nlohmann::json& j, const WorkDoneProgressOptions& o) {
  j = nlohmann::json{};
  to_json_optional(j, "workDoneProgress", o.workDoneProgress);
}

void from_json(const nlohmann::json& j, WorkDoneProgressOptions& o) {
  from_json_optional(j, "workDoneProgress", o.workDoneProgress);
}

// Partial Result Progress
void to_json(nlohmann::json& j, const PartialResultParams& p) {
  j = nlohmann::json{};
  to_json_optional(j, "partialResultToken", p.partialResultToken);
}

void from_json(const nlohmann::json& j, PartialResultParams& p) {
  from_json_optional(j, "partialResultToken", p.partialResultToken);
}

// Trace Value
void to_json(nlohmann::json& j, const TraceValue& t) {
  switch (t) {
    case TraceValue::kOff:
      j = "off";
      break;
    case TraceValue::kMessages:
      j = "messages";
      break;
    case TraceValue::kVerbose:
      j = "verbose";
      break;
    default:
      throw std::runtime_error("Invalid trace value");
  }
}

void from_json(const nlohmann::json& j, TraceValue& t) {
  std::string s = j.get<std::string>();
  if (s == "off") {
    t = TraceValue::kOff;
  } else if (s == "messages") {
    t = TraceValue::kMessages;
  } else if (s == "verbose") {
    t = TraceValue::kVerbose;
  } else {
    throw std::runtime_error("Invalid trace value");
  }
}

// Workspace Folder
void to_json(nlohmann::json& j, const WorkspaceFolder& w) {
  j = nlohmann::json{{"uri", w.uri}, {"name", w.name}};
}

void from_json(const nlohmann::json& j, WorkspaceFolder& w) {
  j.at("uri").get_to(w.uri);
  j.at("name").get_to(w.name);
}

// Symbol Kind
void to_json(nlohmann::json& j, const SymbolKind& k) {
  j = static_cast<int>(k);
}

void from_json(const nlohmann::json& j, SymbolKind& k) {
  k = static_cast<SymbolKind>(j.get<int>());
}

// Symbol Tag
void to_json(nlohmann::json& j, const SymbolTag& t) {
  j = static_cast<int>(t);
}

void from_json(const nlohmann::json& j, SymbolTag& t) {
  t = static_cast<SymbolTag>(j.get<int>());
}

}  // namespace lsp

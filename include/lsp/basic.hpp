#pragma once

#include <optional>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

namespace lsp {

// Cancelation support
struct CancelParams {
  std::string id;
};

void to_json(nlohmann::json& j, const CancelParams& c);
void from_json(const nlohmann::json& j, CancelParams& c);

// Progress support
using ProgressToken = std::string;

template <typename T>
struct ProgressParams {
  ProgressToken token;
  T value;
};

template <typename T>
void to_json(nlohmann::json& j, const ProgressParams<T>& p);
template <typename T>
void from_json(const nlohmann::json& j, ProgressParams<T>& p);

// URI
using Uri = std::string;
using DocumentUri = std::string;

// Regular Expressions
struct RegularExpressionsClientCapabilities {
  std::string engine;
  std::optional<std::string> version;
};

void to_json(nlohmann::json& j, const RegularExpressionsClientCapabilities& c);
void from_json(
    const nlohmann::json& j, RegularExpressionsClientCapabilities& c);

// Position
struct Position {
  int line;
  int character;
};

void to_json(nlohmann::json& j, const Position& p);
void from_json(const nlohmann::json& j, Position& p);

enum class PositionEncodingKind {
  UTF8,
  UTF16,
  UTF32,
};

void to_json(nlohmann::json& j, const PositionEncodingKind& p);
void from_json(const nlohmann::json& j, PositionEncodingKind& p);

// Range
struct Range {
  Position start;
  Position end;
};

void to_json(nlohmann::json& j, const Range& r);
void from_json(const nlohmann::json& j, Range& r);

// Text Document Item
struct TextDocumentItem {
  DocumentUri uri;
  std::string languageId;
  int version;
  std::string text;
};

void to_json(nlohmann::json& j, const TextDocumentItem& t);
void from_json(const nlohmann::json& j, TextDocumentItem& t);

// Text Document Identifier
struct TextDocumentIdentifier {
  DocumentUri uri;
};

void to_json(nlohmann::json& j, const TextDocumentIdentifier& t);
void from_json(const nlohmann::json& j, TextDocumentIdentifier& t);

// Versioned Text Document Identifier
struct VersionedTextDocumentIdentifier : TextDocumentIdentifier {
  int version;
};

void to_json(nlohmann::json& j, const VersionedTextDocumentIdentifier& v);
void from_json(const nlohmann::json& j, VersionedTextDocumentIdentifier& v);

struct OptionalVersionedTextDocumentIdentifier : TextDocumentIdentifier {
  std::optional<int> version;
};

void to_json(
    nlohmann::json& j, const OptionalVersionedTextDocumentIdentifier& o);
void from_json(
    const nlohmann::json& j, OptionalVersionedTextDocumentIdentifier& o);

// Text Document Position Params
struct TextDocumentPositionParams {
  TextDocumentIdentifier textDocument;
  Position position;
};

void to_json(nlohmann::json& j, const TextDocumentPositionParams& t);
void from_json(const nlohmann::json& j, TextDocumentPositionParams& t);

// Document Filter
struct DocumentFilter {
  std::optional<std::string> language;
  std::optional<std::string> scheme;
  std::optional<std::string> pattern;
};

void to_json(nlohmann::json& j, const DocumentFilter& d);
void from_json(const nlohmann::json& j, DocumentFilter& d);

using DocumentSelector = std::vector<DocumentFilter>;

void to_json(nlohmann::json& j, const DocumentSelector& d);
void from_json(const nlohmann::json& j, DocumentSelector& d);

// Text Edit
struct TextEdit {
  Range range;
  std::string newText;
};

void to_json(nlohmann::json& j, const TextEdit& t);
void from_json(const nlohmann::json& j, TextEdit& t);

struct ChangeAnnotation {
  std::string label;
  std::optional<bool> needsConfirmation;
  std::optional<std::string> description;
};

void to_json(nlohmann::json& j, const ChangeAnnotation& c);
void from_json(const nlohmann::json& j, ChangeAnnotation& c);

using ChangeAnnotationIdentifier = std::string;

struct AnnotatedTextEdit : TextEdit {
  ChangeAnnotationIdentifier annotationId;
};

void to_json(nlohmann::json& j, const AnnotatedTextEdit& a);
void from_json(const nlohmann::json& j, AnnotatedTextEdit& a);

// Text Document Edit
using TextEditVariant = std::variant<TextEdit, AnnotatedTextEdit>;

void to_json(nlohmann::json& j, const TextEditVariant& t);
void from_json(const nlohmann::json& j, TextEditVariant& t);

struct TextDocumentEdit {
  std::optional<VersionedTextDocumentIdentifier> textDocument;
  std::vector<TextEditVariant> edits;
};

void to_json(nlohmann::json& j, const TextDocumentEdit& t);
void from_json(const nlohmann::json& j, TextDocumentEdit& t);

// Location
struct Location {
  DocumentUri uri;
  Range range;
};

void to_json(nlohmann::json& j, const Location& l);
void from_json(const nlohmann::json& j, Location& l);

// Location Link
struct LocationLink {
  std::optional<Range> originSelectionRange;
  DocumentUri targetUri;
  Range targetRange;
  Range targetSelectionRange;
};

void to_json(nlohmann::json& j, const LocationLink& l);
void from_json(const nlohmann::json& j, LocationLink& l);

// Diagnostic
enum class DiagnosticSeverity {
  Error = 1,
  Warning = 2,
  Information = 3,
  Hint = 4
};

void to_json(nlohmann::json& j, const DiagnosticSeverity& d);
void from_json(const nlohmann::json& j, DiagnosticSeverity& d);

enum class DiagnosticTag { Unnecessary = 1, Deprecated = 2 };

void to_json(nlohmann::json& j, const DiagnosticTag& d);
void from_json(const nlohmann::json& j, DiagnosticTag& d);

struct CodeDescription {
  std::string href;
};

void to_json(nlohmann::json& j, const CodeDescription& c);
void from_json(const nlohmann::json& j, CodeDescription& c);

struct DiagnosticRelatedInformation {
  Location location;
  std::string message;
};

void to_json(nlohmann::json& j, const DiagnosticRelatedInformation& r);
void from_json(const nlohmann::json& j, DiagnosticRelatedInformation& r);

struct Diagnostic {
  Range range;
  std::optional<DiagnosticSeverity> severity;
  std::optional<std::string> code;
  std::optional<CodeDescription> codeDescription;
  std::optional<std::string> source;
  std::string message;
  std::optional<std::vector<DiagnosticTag>> tags;
  std::optional<std::vector<DiagnosticRelatedInformation>> relatedInformation;
  std::optional<nlohmann::json> data;
};

void to_json(nlohmann::json& j, const Diagnostic& d);
void from_json(const nlohmann::json& j, Diagnostic& d);

// Command
struct Command {
  std::string title;
  std::string command;
  std::optional<nlohmann::json> arguments;
};

void to_json(nlohmann::json& j, const Command& c);
void from_json(const nlohmann::json& j, Command& c);

// Markup Content
enum class MarkupKind { PlainText, Markdown };

void to_json(nlohmann::json& j, const MarkupKind& m);
void from_json(const nlohmann::json& j, MarkupKind& m);

struct MarkupContent {
  MarkupKind kind;
  std::string value;
};

void to_json(nlohmann::json& j, const MarkupContent& m);
void from_json(const nlohmann::json& j, MarkupContent& m);

struct MarkdownClientCapabilities {
  std::string parser;
  std::optional<std::string> version;
  std::optional<std::vector<std::string>> allowedTags;
};

void to_json(nlohmann::json& j, const MarkdownClientCapabilities& m);
void from_json(const nlohmann::json& j, MarkdownClientCapabilities& m);

// File Resource changes
struct CreateFileOptions {
  std::optional<bool> overwrite;
  std::optional<bool> ignoreIfExists;
};

void to_json(nlohmann::json& j, const CreateFileOptions& c);
void from_json(const nlohmann::json& j, CreateFileOptions& c);

struct CreateFile {
  std::string kind = "create";
  DocumentUri uri;
  std::optional<CreateFileOptions> options;
  std::optional<ChangeAnnotationIdentifier> annotationId;
};

void to_json(nlohmann::json& j, const CreateFile& c);
void from_json(const nlohmann::json& j, CreateFile& c);

struct RenameFileOptions {
  std::optional<bool> overwrite;
  std::optional<bool> ignoreIfExists;
};

void to_json(nlohmann::json& j, const RenameFileOptions& r);
void from_json(const nlohmann::json& j, RenameFileOptions& r);

struct RenameFile {
  std::string kind = "rename";
  DocumentUri oldUri;
  DocumentUri newUri;
  std::optional<RenameFileOptions> options;
  std::optional<ChangeAnnotationIdentifier> annotationId;
};

void to_json(nlohmann::json& j, const RenameFile& r);
void from_json(const nlohmann::json& j, RenameFile& r);

struct DeleteFileOptions {
  std::optional<bool> recursive;
  std::optional<bool> ignoreIfNotExists;
};

void to_json(nlohmann::json& j, const DeleteFileOptions& d);
void from_json(const nlohmann::json& j, DeleteFileOptions& d);

struct DeleteFile {
  std::string kind = "delete";
  DocumentUri uri;
  std::optional<DeleteFileOptions> options;
  std::optional<ChangeAnnotationIdentifier> annotationId;
};

void to_json(nlohmann::json& j, const DeleteFile& d);
void from_json(const nlohmann::json& j, DeleteFile& d);

using ChangeAnnotations = std::map<std::string, ChangeAnnotation>;

void to_json(nlohmann::json& j, const ChangeAnnotations& c);
void from_json(const nlohmann::json& j, ChangeAnnotations& c);

using DocumentChange =
    std::variant<TextDocumentEdit, CreateFile, RenameFile, DeleteFile>;

void to_json(nlohmann::json& j, const DocumentChange& d);
void from_json(const nlohmann::json& j, DocumentChange& d);

struct WorkspaceEdit {
  std::optional<std::map<DocumentUri, std::vector<TextEdit>>> changes;
  std::optional<std::vector<DocumentChange>> documentChanges;
  std::optional<ChangeAnnotations> changeAnnotations;
};

void to_json(nlohmann::json& j, const WorkspaceEdit& w);
void from_json(const nlohmann::json& j, WorkspaceEdit& w);

// Workspace Edit Client Capabilities
enum class ResourceOperationKind { Create, Rename, Delete };

void to_json(nlohmann::json& j, const ResourceOperationKind& r);
void from_json(const nlohmann::json& j, ResourceOperationKind& r);

enum class FailureHandlingKind {
  Abort,
  Transactional,
  Undo,
  TextOnlyTransactional
};

void to_json(nlohmann::json& j, const FailureHandlingKind& f);
void from_json(const nlohmann::json& j, FailureHandlingKind& f);

struct ChangeAnnotationSupport {
  std::optional<bool> groupsOnLabel;
};

void to_json(nlohmann::json& j, const ChangeAnnotationSupport& c);
void from_json(const nlohmann::json& j, ChangeAnnotationSupport& c);

struct WorkspaceEditClientCapabilities {
  std::optional<bool> documentChanges;
  std::optional<std::vector<ResourceOperationKind>> resourceOperations;
  std::optional<FailureHandlingKind> failureHandling;
  std::optional<bool> normalizesLineEndings;
  std::optional<ChangeAnnotationSupport> changeAnnotationSupport;
};

void to_json(nlohmann::json& j, const WorkspaceEditClientCapabilities& w);
void from_json(const nlohmann::json& j, WorkspaceEditClientCapabilities& w);

// Work Done Progress
struct WorkDoneProgressBegin {
  std::string kind = "begin";
  std::string title;
  std::optional<bool> cancellable;
  std::optional<std::string> message;
  std::optional<int> percentage;
};

void to_json(nlohmann::json& j, const WorkDoneProgressBegin& w);
void from_json(const nlohmann::json& j, WorkDoneProgressBegin& w);

struct WorkDoneProgressReport {
  std::string kind = "report";
  std::optional<bool> cancellable;
  std::optional<std::string> message;
  std::optional<int> percentage;
};

void to_json(nlohmann::json& j, const WorkDoneProgressReport& w);
void from_json(const nlohmann::json& j, WorkDoneProgressReport& w);

struct WorkDoneProgressEnd {
  std::string kind = "end";
  std::optional<std::string> message;
};

void to_json(nlohmann::json& j, const WorkDoneProgressEnd& w);
void from_json(const nlohmann::json& j, WorkDoneProgressEnd& w);

struct WorkDoneProgressParams {
  std::optional<ProgressToken> workDoneToken;
};

void to_json(nlohmann::json& j, const WorkDoneProgressParams& p);
void from_json(const nlohmann::json& j, WorkDoneProgressParams& p);

struct WorkDoneProgressOptions {
  std::optional<bool> workDoneProgress;
};

void to_json(nlohmann::json& j, const WorkDoneProgressOptions& o);
void from_json(const nlohmann::json& j, WorkDoneProgressOptions& o);

// Partial Result Progress
struct PartialResultParams {
  std::optional<ProgressToken> partialResultToken;
};

void to_json(nlohmann::json& j, const PartialResultParams& p);
void from_json(const nlohmann::json& j, PartialResultParams& p);

// Trace Value
enum class TraceValue { Off, Messages, Verbose };

void to_json(nlohmann::json& j, const TraceValue& t);
void from_json(const nlohmann::json& j, TraceValue& t);

// Workspace Folder
struct WorkspaceFolder {
  DocumentUri uri;
  std::string name;
};

void to_json(nlohmann::json& j, const WorkspaceFolder& w);
void from_json(const nlohmann::json& j, WorkspaceFolder& w);

// Symbol Kind
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

void to_json(nlohmann::json& j, const SymbolKind& k);
void from_json(const nlohmann::json& j, SymbolKind& k);

enum class SymbolTag { Deprecated = 1 };

void to_json(nlohmann::json& j, const SymbolTag& t);
void from_json(const nlohmann::json& j, SymbolTag& t);

}  // namespace lsp

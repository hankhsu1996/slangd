#pragma once

#include <nlohmann/json.hpp>

namespace lsp {

/**
 * Cancel Params
 */
struct CancelParams {
  std::string id;
};

/**
 * Progress Token
 */
using ProgressToken = std::string;

/**
 * Progress Params
 */
template <typename T>
struct ProgressParams {
  ProgressToken token;
  T value;
};

/**
 * URI
 */
using Uri = std::string;

/**
 * Document URI
 */
using DocumentUri = std::string;

/**
 * Regular expression client capabilities
 */
struct RegularExpressionsClientCapabilities {
  std::string engine;
  std::optional<std::string> version;
};

/**
 * Position in a text document
 */
struct Position {
  int line;
  int character;
};

/**
 * Range in a text document
 */
struct Range {
  Position start;
  Position end;
};

struct TextDocumentItem {
  DocumentUri uri;
  std::string language_id;
  int version;
  std::string text;
};

/**
 * Text Document Identifier
 */
struct TextDocumentIdentifier {
  DocumentUri uri;
};

/**
 * Versioned Text Document Identifier
 */
struct VersionedTextDocumentIdentifier : TextDocumentIdentifier {
  int version;
};

/**
 * Optional Versioned Text Document Identifier
 */
struct OptionalVersionedTextDocumentIdentifier : TextDocumentIdentifier {
  std::optional<int> version;
};

/**
 * Text Document Position Params
 */
struct TextDocumentPositionParams {
  TextDocumentIdentifier textDocument;
  Position position;
};

/**
 * Document Filter
 */
struct DocumentFilter {
  std::optional<std::string> language;
  std::optional<std::string> scheme;
  std::optional<std::string> pattern;
};

/**
 * Document Selector
 */
using DocumentSelector = std::vector<DocumentFilter>;

/**
 * Work Done Progress Begin
 */
struct WorkDoneProgressBegin {
  std::string kind = "begin";
  std::string title;
  std::optional<bool> cancellable;
  std::optional<std::string> message;
  std::optional<int> percentage;
};

/**
 * Work Done Progress Report
 */
struct WorkDoneProgressReport {
  std::string kind = "report";
  std::optional<bool> cancellable;
  std::optional<std::string> message;
  std::optional<int> percentage;
};

/**
 * Work Done Progress End
 */
struct WorkDoneProgressEnd {
  std::string kind = "end";
  std::optional<std::string> message;
};

/**
 * Work Done Progress Params
 */
struct WorkDoneProgressParams {
  std::optional<ProgressToken> workDoneToken;
};

/**
 * Work Done Progress Options
 */
struct WorkDoneProgressOptions {
  std::optional<bool> workDoneProgress;
};

/**
 * Partial Result Params
 */
struct PartialResultParams {
  std::optional<ProgressToken> partialResultToken;
};

/**
 * Trace Value
 */
enum class TraceValue { Off, Messages, Verbose };

// JSON conversion
void to_json(nlohmann::json& j, const CancelParams& c);
void from_json(const nlohmann::json& j, CancelParams& c);

template <typename T>
void to_json(nlohmann::json& j, const ProgressParams<T>& p);
template <typename T>
void from_json(const nlohmann::json& j, ProgressParams<T>& p);

void to_json(nlohmann::json& j, const RegularExpressionsClientCapabilities& c);
void from_json(
    const nlohmann::json& j, RegularExpressionsClientCapabilities& c);

void to_json(nlohmann::json& j, const Position& p);
void from_json(const nlohmann::json& j, Position& p);

void to_json(nlohmann::json& j, const Range& r);
void from_json(const nlohmann::json& j, Range& r);

void to_json(nlohmann::json& j, const TextDocumentItem& t);
void from_json(const nlohmann::json& j, TextDocumentItem& t);

void to_json(nlohmann::json& j, const TextDocumentIdentifier& t);
void from_json(const nlohmann::json& j, TextDocumentIdentifier& t);

void to_json(nlohmann::json& j, const VersionedTextDocumentIdentifier& v);
void from_json(const nlohmann::json& j, VersionedTextDocumentIdentifier& v);

void to_json(
    nlohmann::json& j, const OptionalVersionedTextDocumentIdentifier& o);
void from_json(
    const nlohmann::json& j, OptionalVersionedTextDocumentIdentifier& o);

void to_json(nlohmann::json& j, const TextDocumentPositionParams& t);
void from_json(const nlohmann::json& j, TextDocumentPositionParams& t);

void to_json(nlohmann::json& j, const DocumentFilter& d);
void from_json(const nlohmann::json& j, DocumentFilter& d);

void to_json(nlohmann::json& j, const DocumentSelector& d);
void from_json(const nlohmann::json& j, DocumentSelector& d);

void to_json(nlohmann::json& j, const WorkDoneProgressBegin& w);
void from_json(const nlohmann::json& j, WorkDoneProgressBegin& w);

void to_json(nlohmann::json& j, const WorkDoneProgressReport& w);
void from_json(const nlohmann::json& j, WorkDoneProgressReport& w);

void to_json(nlohmann::json& j, const WorkDoneProgressEnd& w);
void from_json(const nlohmann::json& j, WorkDoneProgressEnd& w);

void to_json(nlohmann::json& j, const WorkDoneProgressParams& p);
void from_json(const nlohmann::json& j, WorkDoneProgressParams& p);

void to_json(nlohmann::json& j, const WorkDoneProgressOptions& o);
void from_json(const nlohmann::json& j, WorkDoneProgressOptions& o);

void to_json(nlohmann::json& j, const PartialResultParams& p);
void from_json(const nlohmann::json& j, PartialResultParams& p);

void to_json(nlohmann::json& j, const TraceValue& t);
void from_json(const nlohmann::json& j, TraceValue& t);

}  // namespace lsp

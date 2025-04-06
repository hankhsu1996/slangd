#pragma once

#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "lsp/basic.hpp"
#include "lsp/json_utils.hpp"
namespace lsp {

// Document Highlights Request
struct DocumentHighlightParams : TextDocumentPositionParams,
                                 WorkDoneProgressParams,
                                 PartialResultParams {};

enum class DocumentHighlightKind { kText = 1, kRead = 2, kWrite = 3 };

struct DocumentHighlight {
  Range range{};
  std::optional<DocumentHighlightKind> kind;
};

using DocumentHighlightResult = std::optional<std::vector<DocumentHighlight>>;

// Document Link Request
struct DocumentLinkParams : WorkDoneProgressParams, PartialResultParams {
  TextDocumentIdentifier textDocument;
};

struct DocumentLink {
  Range range{};
  std::optional<DocumentUri> target;
  std::optional<std::string> tooltip;
  std::optional<nlohmann::json> data;
};

using DocumentLinkResult = std::optional<std::vector<DocumentLink>>;

// Document Link Resolve Request
using DocumentLinkResolveParams = DocumentLink;

using DocumentLinkResolveResult = DocumentLink;

// Hover Request
struct HoverParams : TextDocumentPositionParams, WorkDoneProgressParams {};

struct MarkedCode {
  std::string language;
  std::string value;
};

using MarkedString = std::variant<std::string, MarkedCode>;

struct Hover {
  std::variant<MarkedString, std::vector<MarkedString>, MarkupContent> contents;
  std::optional<Range> range;
};

using HoverResult = std::optional<Hover>;

// Code Lens Request
struct CodeLensParams : WorkDoneProgressParams, PartialResultParams {
  TextDocumentIdentifier textDocument;
};

struct CodeLens {
  Range range{};
  std::optional<Command> command;
  std::optional<nlohmann::json> data;
};

using CodeLensResult = std::optional<std::vector<CodeLens>>;

// Code Lens Resolve Request
using CodeLensResolveParams = CodeLens;

using CodeLensResolveResult = CodeLens;

// Code Lens Refresh Request
struct CodeLensRefreshParams {};

struct CodeLensRefreshResult {};

// Folding Range Request
struct FoldingRangeParams : WorkDoneProgressParams, PartialResultParams {
  TextDocumentIdentifier textDocument;
};

enum class FoldingRangeKind { kComment, kImports, kRegion };

struct FoldingRange {
  int startLine{};
  std::optional<int> startCharacter;
  int endLine{};
  std::optional<int> endCharacter;
  std::optional<FoldingRangeKind> kind;
  std::optional<std::string> collapsedText;
};

using FoldingRangeResult = std::optional<std::vector<FoldingRange>>;

// Selection Range Request
struct SelectionRangeParams : WorkDoneProgressParams, PartialResultParams {
  TextDocumentIdentifier textDocument;
  std::vector<Position> positions;
};

struct SelectionRange {
  Range range;
  std::shared_ptr<SelectionRange> parent;
};

using SelectionRangeResult = std::optional<std::vector<SelectionRange>>;

// Document Symbols Request
struct DocumentSymbolParams : WorkDoneProgressParams, PartialResultParams {
  TextDocumentIdentifier textDocument;
};

inline void to_json(nlohmann::json& j, const DocumentSymbolParams& p) {
  to_json_required(j, "textDocument", p.textDocument);
}

inline void from_json(const nlohmann::json& j, DocumentSymbolParams& p) {
  from_json_required(j, "textDocument", p.textDocument);
}

struct DocumentSymbol {
  std::string name;
  std::optional<std::string> detail;
  SymbolKind kind;
  std::optional<std::vector<SymbolTag>> tags;
  std::optional<bool> deprecated;
  Range range;
  Range selectionRange;
  std::optional<std::vector<DocumentSymbol>> children;
};

inline void to_json(nlohmann::json& j, const DocumentSymbol& s) {
  to_json_required(j, "name", s.name);
  to_json_optional(j, "detail", s.detail);
  to_json_required(j, "kind", s.kind);
  to_json_optional(j, "tags", s.tags);
  to_json_optional(j, "deprecated", s.deprecated);
  to_json_required(j, "range", s.range);
  to_json_required(j, "selectionRange", s.selectionRange);
  to_json_optional(j, "children", s.children);
}

inline void from_json(const nlohmann::json& j, DocumentSymbol& s) {
  from_json_required(j, "name", s.name);
  from_json_optional(j, "detail", s.detail);
  from_json_required(j, "kind", s.kind);
  from_json_optional(j, "tags", s.tags);
  from_json_optional(j, "deprecated", s.deprecated);
  from_json_required(j, "range", s.range);
  from_json_required(j, "selectionRange", s.selectionRange);
  from_json_optional(j, "children", s.children);
}

struct SymbolInformation {
  std::string name;
  SymbolKind kind;
  std::optional<std::vector<SymbolTag>> tags;
  std::optional<bool> deprecated;
  Location location;
  std::optional<std::string> containerName;
};

inline void to_json(nlohmann::json& j, const SymbolInformation& s) {
  to_json_required(j, "name", s.name);
  to_json_required(j, "kind", s.kind);
  to_json_optional(j, "tags", s.tags);
  to_json_optional(j, "deprecated", s.deprecated);
  to_json_required(j, "location", s.location);
  to_json_optional(j, "containerName", s.containerName);
}

inline void from_json(const nlohmann::json& j, SymbolInformation& s) {
  from_json_required(j, "name", s.name);
  from_json_required(j, "kind", s.kind);
  from_json_optional(j, "tags", s.tags);
  from_json_optional(j, "deprecated", s.deprecated);
  from_json_required(j, "location", s.location);
  from_json_optional(j, "containerName", s.containerName);
}

using DocumentSymbolResult = std::optional<
    std::variant<std::vector<DocumentSymbol>, std::vector<SymbolInformation>>>;

inline void to_json(nlohmann::json& j, const DocumentSymbolResult& r) {
  if (r.has_value()) {
    std::visit([&j](auto&& arg) { to_json(j, arg); }, r.value());
  } else {
    j = nullptr;
  }
}

inline void from_json(const nlohmann::json& j, DocumentSymbolResult& r) {
  // TODO(hankhsu1996): Implement this
  if (j.is_null()) {
    r = std::nullopt;
  } else {
    if (j.is_array()) {
      if (j.empty()) {
        r = std::vector<DocumentSymbol>();
      } else {
        r = j.get<std::vector<DocumentSymbol>>();
      }
    }
  }
}

// Semantic Tokens
enum class SemanticTokenTypes {
  kNamespace,
  kType,
  kClass,
  kEnum,
  kInterface,
  kStruct,
  kTypeParameter,
  kParameter,
  kVariable,
  kProperty,
  kEnumMember,
  kEvent,
  kFunction,
  kMethod,
  kMacro,
  kKeyword,
  kModifier,
  kComment,
  kString,
  kNumber,
  kRegexp,
  kOperator,
  kDecorator
};

void to_json(nlohmann::json& j, const SemanticTokenTypes& t);
void from_json(const nlohmann::json& j, SemanticTokenTypes& t);

enum class SemanticTokenModifiers {
  kDeclaration,
  kDefinition,
  kReadonly,
  kStatic,
  kDeprecated,
  kAbstract,
  kAsync,
  kModification,
  kDocumentation,
  kDefaultLibrary
};

enum class TokenFormat {
  kRelative,
};

struct SemanticTokensLegend {
  std::vector<std::string> tokenTypes;
  std::vector<std::string> tokenModifiers;
};

struct SemanticTokensParams : WorkDoneProgressParams, PartialResultParams {
  TextDocumentIdentifier textDocument;
};

struct SemanticTokens {
  std::optional<std::string> resultId;
  std::vector<int> data;
};

struct SemanticTokensDeltaParams : WorkDoneProgressParams, PartialResultParams {
  TextDocumentIdentifier textDocument;
  std::string previousResultId;
};

struct SemanticTokensEdit {
  int start{};
  int deleteCount{};
  std::optional<std::vector<int>> data;
};

struct SemanticTokensDelta {
  std::optional<std::string> resultId;
  std::vector<SemanticTokensEdit> edits;
};

using SemanticTokensResult =
    std::optional<std::variant<SemanticTokens, SemanticTokensDelta>>;

struct SemanticTokensRangeParams : WorkDoneProgressParams, PartialResultParams {
  TextDocumentIdentifier textDocument;
  Range range{};
};

using SemanticTokensRangeResult = std::optional<SemanticTokens>;

struct SemanticTokensRefreshParams {};

struct SemanticTokensRefreshResult {};

// Inlay Hint Request
struct InlayHintParams : WorkDoneProgressParams {
  TextDocumentIdentifier textDocument;
  Range range{};
};

enum class InlayHintKind { kType = 1, kParameter = 2 };

struct InlayHintLabelPart {
  std::string value;
  std::optional<MarkupContent> tooltip;
  std::optional<Location> location;
  std::optional<Command> command;
};

struct InlayHint {
  Position position;
  std::variant<std::string, std::vector<InlayHintLabelPart>> label;
  std::optional<InlayHintKind> kind;
  std::optional<std::vector<TextEdit>> textEdits;
  std::optional<std::string> tooltip;
  std::optional<bool> paddingLeft;
  std::optional<bool> paddingRight;
  std::optional<nlohmann::json> data;
};

using InlayHintResult = std::optional<std::vector<InlayHint>>;

// Inlay Hint Resolve Request
using InlayHintResolveParams = InlayHint;

using InlayHintResolveResult = InlayHint;

// Inlay Hint Refresh Request
struct InlayHintRefreshParams {};

struct InlayHintRefreshResult {};

// Inline Value Request
struct InlineValueContext {
  int frameId;
  Range stoppedLocation;
};

struct InlineValueParams : WorkDoneProgressParams {
  TextDocumentIdentifier textDocument;
  Range range{};
  InlineValueContext context{};
};

struct InlineValueText {
  Range range;
  std::string text;
};

struct InlineValueVariableLookup {
  Range range{};
  std::optional<std::string> variableName;
  bool caseSensitiveLookup{};
};

struct InlineValueEvaluatableExpression {
  Range range{};
  std::optional<std::string> expression;
};

using InlineValue = std::variant<
    InlineValueText, InlineValueVariableLookup,
    InlineValueEvaluatableExpression>;

// Inline Value Refresh Request
struct InlineValueRefreshParams {};

struct InlineValueRefreshResult {};

// Monikers
struct MonikerParams : TextDocumentPositionParams,
                       WorkDoneProgressParams,
                       PartialResultParams {};

enum class UniquenessLevel { kDocument, kProject, kGroup, kScheme, kGlobal };

enum class MonikerKind { kImport, kExport, kLocal };

struct Moniker {
  std::string scheme;
  std::string identifier;
  UniquenessLevel unique;
  std::optional<MonikerKind> kind;
};

// Completion Request

enum class CompletionTriggerKind {
  kInvoked,
  kTriggerCharacter,
  kTriggerForIncompleteCompletions
};

struct CompletionContext {
  CompletionTriggerKind triggerKind;
  std::optional<std::string> triggerCharacter;
};

void to_json(nlohmann::json& j, const CompletionContext& c);
void from_json(const nlohmann::json& j, CompletionContext& c);

struct CompletionParams : TextDocumentPositionParams,
                          WorkDoneProgressParams,
                          PartialResultParams {
  std::optional<CompletionContext> context;
};

enum class InsertTextFormat { kPlainText, kSnippet };

enum class CompletionItemTag { kDeprecated };

struct InsertReplaceEdit {
  std::string newText;
  Range insert;
  Range replace;
};

enum class InsertTextMode { kAsIs, kAdjustIndentation };

struct CompletionItemLabelDetails {
  std::optional<std::string> detail;
  std::optional<std::string> description;
};

enum class CompletionItemKind {
  kText,
  kMethod,
  kFunction,
  kConstructor,
  kField,
  kVariable,
  kClass,
  kInterface,
  kModule,
  kProperty,
  kUnit,
  kValue,
  kEnum,
  kKeyword,
  kSnippet,
  kColor,
  kFile,
  kReference,
  kFolder,
  kEnumMember,
  kConstant,
  kStruct,
  kEvent,
  kOperator,
  kTypeParameter,
};

struct CompletionItem {
  std::string label;
  std::optional<CompletionItemLabelDetails> labelDetails;
  std::optional<CompletionItemKind> kind;
  std::optional<std::vector<CompletionItemTag>> tags;
  std::optional<std::string> detail;
  std::optional<std::variant<std::string, MarkupContent>> documentation;
  std::optional<bool> deprecated;
  std::optional<bool> preselect;
  std::optional<std::string> sortText;
  std::optional<std::string> filterText;
  std::optional<TextEdit> textEdit;
  std::optional<InsertReplaceEdit> insertReplaceEdit;
  std::optional<std::vector<TextEdit>> additionalTextEdits;
  std::optional<std::vector<std::string>> commitCharacters;
  std::optional<Command> command;
  std::optional<nlohmann::json> data;
};

struct CompletionList {
  bool isIncomplete;

  struct CompletionItemDefaults {
    std::optional<std::vector<std::string>> commitCharacters;
    struct EditRangeInsertReplace {
      Range insert;
      Range replace;
    };
    using EditRange = std::variant<Range, EditRangeInsertReplace>;
    std::optional<EditRange> editRange;
    std::optional<InsertTextFormat> insertTextFormat;
    std::optional<InsertTextMode> insertTextMode;
    std::optional<nlohmann::json> data;
  };
  std::optional<CompletionItemDefaults> itemDefaults;
  std::vector<CompletionItem> items;
};

using CompletionResponse =
    std::variant<std::vector<CompletionItem>, CompletionList>;

// Completion Item Resolve Request
using CompletionItemResolveParams = CompletionItem;
using CompletionItemResolveResponse = CompletionItem;

}  // namespace lsp

#pragma once

#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "lsp/basic.hpp"

namespace lsp {

// Workspace specific client capabilities
struct DidChangeConfigurationClientCapabilities {};

void to_json(
    nlohmann::json& j, const DidChangeConfigurationClientCapabilities& c);
void from_json(
    const nlohmann::json& j, DidChangeConfigurationClientCapabilities& c);

struct DidChangeWatchedFilesClientCapabilities {
  std::optional<bool> dynamicRegistration;
  std::optional<bool> relativePatternSupport;
};

void to_json(
    nlohmann::json& j, const DidChangeWatchedFilesClientCapabilities& c);
void from_json(
    const nlohmann::json& j, DidChangeWatchedFilesClientCapabilities& c);

struct WorkspaceSymbolClientCapabilities {};

void to_json(nlohmann::json& j, const WorkspaceSymbolClientCapabilities& c);
void from_json(const nlohmann::json& j, WorkspaceSymbolClientCapabilities& c);

struct ExecuteCommandClientCapabilities {};

void to_json(nlohmann::json& j, const ExecuteCommandClientCapabilities& c);
void from_json(const nlohmann::json& j, ExecuteCommandClientCapabilities& c);

struct SemanticTokensWorkspaceClientCapabilities {};

void to_json(
    nlohmann::json& j, const SemanticTokensWorkspaceClientCapabilities& c);
void from_json(
    const nlohmann::json& j, SemanticTokensWorkspaceClientCapabilities& c);

struct CodeLensWorkspaceClientCapabilities {};

void to_json(nlohmann::json& j, const CodeLensWorkspaceClientCapabilities& c);
void from_json(const nlohmann::json& j, CodeLensWorkspaceClientCapabilities& c);

struct InlineValueWorkspaceClientCapabilities {};

void to_json(
    nlohmann::json& j, const InlineValueWorkspaceClientCapabilities& c);
void from_json(
    const nlohmann::json& j, InlineValueWorkspaceClientCapabilities& c);

struct InlayHintWorkspaceClientCapabilities {};

void to_json(nlohmann::json& j, const InlayHintWorkspaceClientCapabilities& c);
void from_json(
    const nlohmann::json& j, InlayHintWorkspaceClientCapabilities& c);

struct DiagnosticWorkspaceClientCapabilities {};

void to_json(nlohmann::json& j, const DiagnosticWorkspaceClientCapabilities& c);
void from_json(
    const nlohmann::json& j, DiagnosticWorkspaceClientCapabilities& c);

struct WorkspaceClientCapabilities {
  std::optional<bool> applyEdit;
  std::optional<WorkspaceEditClientCapabilities> workspaceEdit;
  std::optional<DidChangeConfigurationClientCapabilities>
      didChangeConfiguration;
  std::optional<DidChangeWatchedFilesClientCapabilities> didChangeWatchedFiles;
  std::optional<WorkspaceSymbolClientCapabilities> symbol;
  std::optional<ExecuteCommandClientCapabilities> executeCommand;
  std::optional<bool> workspaceFolders;
  std::optional<bool> configuration;
  std::optional<SemanticTokensWorkspaceClientCapabilities> semanticTokens;
  std::optional<CodeLensWorkspaceClientCapabilities> codeLens;
  struct FileOperations {
    std::optional<bool> dynamicRegistration;
    std::optional<bool> didCreate;
    std::optional<bool> willCreate;
    std::optional<bool> didRename;
    std::optional<bool> willRename;
    std::optional<bool> didDelete;
    std::optional<bool> willDelete;
  };
  std::optional<FileOperations> fileOperations;
  std::optional<InlineValueWorkspaceClientCapabilities> inlineValue;
  std::optional<InlayHintWorkspaceClientCapabilities> inlayHint;
  std::optional<DiagnosticWorkspaceClientCapabilities> diagnostics;
};

void to_json(
    nlohmann::json& j, const WorkspaceClientCapabilities::FileOperations& c);
void from_json(
    const nlohmann::json& j, WorkspaceClientCapabilities::FileOperations& c);

void to_json(nlohmann::json& j, const WorkspaceClientCapabilities& c);
void from_json(const nlohmann::json& j, WorkspaceClientCapabilities& c);

// Text document specific client capabilities
struct TextDocumentSyncClientCapabilities {};

void to_json(nlohmann::json& j, const TextDocumentSyncClientCapabilities& c);
void from_json(const nlohmann::json& j, TextDocumentSyncClientCapabilities& c);

struct CompletionClientCapabilities {};

void to_json(nlohmann::json& j, const CompletionClientCapabilities& c);
void from_json(const nlohmann::json& j, CompletionClientCapabilities& c);

struct HoverClientCapabilities {};

void to_json(nlohmann::json& j, const HoverClientCapabilities& c);
void from_json(const nlohmann::json& j, HoverClientCapabilities& c);

struct SignatureHelpClientCapabilities {};

void to_json(nlohmann::json& j, const SignatureHelpClientCapabilities& c);
void from_json(const nlohmann::json& j, SignatureHelpClientCapabilities& c);

struct DeclarationClientCapabilities {};

void to_json(nlohmann::json& j, const DeclarationClientCapabilities& c);
void from_json(const nlohmann::json& j, DeclarationClientCapabilities& c);

struct DefinitionClientCapabilities {};

void to_json(nlohmann::json& j, const DefinitionClientCapabilities& c);
void from_json(const nlohmann::json& j, DefinitionClientCapabilities& c);

struct TypeDefinitionClientCapabilities {};

void to_json(nlohmann::json& j, const TypeDefinitionClientCapabilities& c);
void from_json(const nlohmann::json& j, TypeDefinitionClientCapabilities& c);

struct ImplementationClientCapabilities {};

void to_json(nlohmann::json& j, const ImplementationClientCapabilities& c);
void from_json(const nlohmann::json& j, ImplementationClientCapabilities& c);

struct ReferenceClientCapabilities {};

void to_json(nlohmann::json& j, const ReferenceClientCapabilities& c);
void from_json(const nlohmann::json& j, ReferenceClientCapabilities& c);

struct DocumentHighlightClientCapabilities {};

void to_json(nlohmann::json& j, const DocumentHighlightClientCapabilities& c);
void from_json(const nlohmann::json& j, DocumentHighlightClientCapabilities& c);

struct DocumentSymbolClientCapabilities {};

void to_json(nlohmann::json& j, const DocumentSymbolClientCapabilities& c);
void from_json(const nlohmann::json& j, DocumentSymbolClientCapabilities& c);

struct CodeActionClientCapabilities {};

void to_json(nlohmann::json& j, const CodeActionClientCapabilities& c);
void from_json(const nlohmann::json& j, CodeActionClientCapabilities& c);

struct CodeLensClientCapabilities {};

void to_json(nlohmann::json& j, const CodeLensClientCapabilities& c);
void from_json(const nlohmann::json& j, CodeLensClientCapabilities& c);

struct DocumentLinkClientCapabilities {};

void to_json(nlohmann::json& j, const DocumentLinkClientCapabilities& c);
void from_json(const nlohmann::json& j, DocumentLinkClientCapabilities& c);

struct DocumentColorClientCapabilities {};

void to_json(nlohmann::json& j, const DocumentColorClientCapabilities& c);
void from_json(const nlohmann::json& j, DocumentColorClientCapabilities& c);

struct DocumentFormattingClientCapabilities {};

void to_json(nlohmann::json& j, const DocumentFormattingClientCapabilities& c);
void from_json(
    const nlohmann::json& j, DocumentFormattingClientCapabilities& c);

struct DocumentRangeFormattingClientCapabilities {};

void to_json(
    nlohmann::json& j, const DocumentRangeFormattingClientCapabilities& c);
void from_json(
    const nlohmann::json& j, DocumentRangeFormattingClientCapabilities& c);

struct DocumentOnTypeFormattingClientCapabilities {};

void to_json(
    nlohmann::json& j, const DocumentOnTypeFormattingClientCapabilities& c);
void from_json(
    const nlohmann::json& j, DocumentOnTypeFormattingClientCapabilities& c);

struct RenameClientCapabilities {};

void to_json(nlohmann::json& j, const RenameClientCapabilities& c);
void from_json(const nlohmann::json& j, RenameClientCapabilities& c);

struct PublishDiagnosticsClientCapabilities {};

void to_json(nlohmann::json& j, const PublishDiagnosticsClientCapabilities& c);
void from_json(
    const nlohmann::json& j, PublishDiagnosticsClientCapabilities& c);

struct FoldingRangeClientCapabilities {};

void to_json(nlohmann::json& j, const FoldingRangeClientCapabilities& c);
void from_json(const nlohmann::json& j, FoldingRangeClientCapabilities& c);

struct SelectionRangeClientCapabilities {};

void to_json(nlohmann::json& j, const SelectionRangeClientCapabilities& c);
void from_json(const nlohmann::json& j, SelectionRangeClientCapabilities& c);

struct LinkedEditingRangeClientCapabilities {};

void to_json(nlohmann::json& j, const LinkedEditingRangeClientCapabilities& c);
void from_json(
    const nlohmann::json& j, LinkedEditingRangeClientCapabilities& c);

struct CallHierarchyClientCapabilities {};

void to_json(nlohmann::json& j, const CallHierarchyClientCapabilities& c);
void from_json(const nlohmann::json& j, CallHierarchyClientCapabilities& c);

struct SemanticTokensClientCapabilities {};

void to_json(nlohmann::json& j, const SemanticTokensClientCapabilities& c);
void from_json(const nlohmann::json& j, SemanticTokensClientCapabilities& c);

struct MonikerClientCapabilities {};

void to_json(nlohmann::json& j, const MonikerClientCapabilities& c);
void from_json(const nlohmann::json& j, MonikerClientCapabilities& c);

struct TypeHierarchyClientCapabilities {};

void to_json(nlohmann::json& j, const TypeHierarchyClientCapabilities& c);
void from_json(const nlohmann::json& j, TypeHierarchyClientCapabilities& c);

struct InlineValueClientCapabilities {};

void to_json(nlohmann::json& j, const InlineValueClientCapabilities& c);
void from_json(const nlohmann::json& j, InlineValueClientCapabilities& c);

struct InlayHintClientCapabilities {};

void to_json(nlohmann::json& j, const InlayHintClientCapabilities& c);
void from_json(const nlohmann::json& j, InlayHintClientCapabilities& c);

struct DiagnosticClientCapabilities {};

void to_json(nlohmann::json& j, const DiagnosticClientCapabilities& c);
void from_json(const nlohmann::json& j, DiagnosticClientCapabilities& c);

struct TextDocumentClientCapabilities {
  std::optional<TextDocumentSyncClientCapabilities> synchronization;
  std::optional<CompletionClientCapabilities> completion;
  std::optional<HoverClientCapabilities> hover;
  std::optional<SignatureHelpClientCapabilities> signatureHelp;
  std::optional<DeclarationClientCapabilities> declaration;
  std::optional<DefinitionClientCapabilities> definition;
  std::optional<TypeDefinitionClientCapabilities> typeDefinition;
  std::optional<ImplementationClientCapabilities> implementation;
  std::optional<ReferenceClientCapabilities> references;
  std::optional<DocumentHighlightClientCapabilities> documentHighlight;
  std::optional<DocumentSymbolClientCapabilities> documentSymbol;
  std::optional<CodeActionClientCapabilities> codeAction;
  std::optional<CodeLensClientCapabilities> codeLens;
  std::optional<DocumentLinkClientCapabilities> documentLink;
  std::optional<DocumentColorClientCapabilities> colorProvider;
  std::optional<DocumentFormattingClientCapabilities> formatting;
  std::optional<DocumentRangeFormattingClientCapabilities> rangeFormatting;
  std::optional<DocumentOnTypeFormattingClientCapabilities> onTypeFormatting;
  std::optional<RenameClientCapabilities> rename;
  std::optional<PublishDiagnosticsClientCapabilities> publishDiagnostics;
  std::optional<FoldingRangeClientCapabilities> foldingRange;
  std::optional<SelectionRangeClientCapabilities> selectionRange;
  std::optional<LinkedEditingRangeClientCapabilities> linkedEditingRange;
  std::optional<CallHierarchyClientCapabilities> callHierarchy;
  std::optional<SemanticTokensClientCapabilities> semanticTokens;
  std::optional<MonikerClientCapabilities> moniker;
  std::optional<TypeHierarchyClientCapabilities> typeHierarchy;
  std::optional<InlineValueClientCapabilities> inlineValue;
  std::optional<InlayHintClientCapabilities> inlayHint;
  std::optional<DiagnosticClientCapabilities> diagnostic;
};

void to_json(nlohmann::json& j, const TextDocumentClientCapabilities& c);
void from_json(const nlohmann::json& j, TextDocumentClientCapabilities& c);

// Capabilities specific to the notebook document support
struct NotebookDocumentSyncClientCapabilities {};

void to_json(
    nlohmann::json& j, const NotebookDocumentSyncClientCapabilities& c);
void from_json(
    const nlohmann::json& j, NotebookDocumentSyncClientCapabilities& c);

struct NotebookDocumentClientCapabilities {
  std::optional<NotebookDocumentSyncClientCapabilities> synchronization;
};

void to_json(nlohmann::json& j, const NotebookDocumentClientCapabilities& c);
void from_json(const nlohmann::json& j, NotebookDocumentClientCapabilities& c);

// Window specific client capabilities
struct ShowMessageRequestClientCapabilities {};

void to_json(nlohmann::json& j, const ShowMessageRequestClientCapabilities& c);
void from_json(
    const nlohmann::json& j, ShowMessageRequestClientCapabilities& c);

struct ShowDocumentClientCapabilities {};

void to_json(nlohmann::json& j, const ShowDocumentClientCapabilities& c);
void from_json(const nlohmann::json& j, ShowDocumentClientCapabilities& c);

struct WindowClientCapabilities {
  std::optional<bool> workDoneProgress;
  std::optional<ShowMessageRequestClientCapabilities> showMessage;
  std::optional<ShowDocumentClientCapabilities> showDocument;
};

void to_json(nlohmann::json& j, const WindowClientCapabilities& c);
void from_json(const nlohmann::json& j, WindowClientCapabilities& c);

// General client capabilities
struct StaleRequestSupport {
  bool cancel;
  std::vector<std::string> retryOnContentModified;
};

void to_json(nlohmann::json& j, const StaleRequestSupport& c);
void from_json(const nlohmann::json& j, StaleRequestSupport& c);

struct GeneralClientCapabilities {
  std::optional<StaleRequestSupport> staleRequestSupport;
  std::optional<RegularExpressionsClientCapabilities> regularExpressions;
  std::optional<MarkdownClientCapabilities> markdown;
  std::optional<std::vector<PositionEncodingKind>> positionEncodings;
};

void to_json(nlohmann::json& j, const GeneralClientCapabilities& c);
void from_json(const nlohmann::json& j, GeneralClientCapabilities& c);

struct ClientCapabilities {
  std::optional<WorkspaceClientCapabilities> workspace;
  std::optional<TextDocumentClientCapabilities> textDocument;
  std::optional<NotebookDocumentClientCapabilities> notebookDocument;
  std::optional<WindowClientCapabilities> window;
  std::optional<GeneralClientCapabilities> general;
  std::optional<nlohmann::json> experimental;
};

void to_json(nlohmann::json& j, const ClientCapabilities& c);
void from_json(const nlohmann::json& j, ClientCapabilities& c);

}  // namespace lsp

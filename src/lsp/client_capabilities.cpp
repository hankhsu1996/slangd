#include "lsp/client_capabilities.hpp"

#include "lsp/json_utils.hpp"

namespace lsp {

// Workspace specific client capabilities
void to_json(
    nlohmann::json& j, const DidChangeConfigurationClientCapabilities& c) {}

void from_json(
    const nlohmann::json& j, DidChangeConfigurationClientCapabilities& c) {}

void to_json(
    nlohmann::json& j, const DidChangeWatchedFilesClientCapabilities& c) {}

void from_json(
    const nlohmann::json& j, DidChangeWatchedFilesClientCapabilities& c) {}

void to_json(nlohmann::json& j, const WorkspaceSymbolClientCapabilities& c) {}

void from_json(const nlohmann::json& j, WorkspaceSymbolClientCapabilities& c) {}

void to_json(nlohmann::json& j, const ExecuteCommandClientCapabilities& c) {}

void from_json(const nlohmann::json& j, ExecuteCommandClientCapabilities& c) {}

void to_json(
    nlohmann::json& j, const SemanticTokensWorkspaceClientCapabilities& c) {}

void from_json(
    const nlohmann::json& j, SemanticTokensWorkspaceClientCapabilities& c) {}

void to_json(nlohmann::json& j, const CodeLensWorkspaceClientCapabilities& c) {}

void from_json(
    const nlohmann::json& j, CodeLensWorkspaceClientCapabilities& c) {}

void to_json(
    nlohmann::json& j, const InlineValueWorkspaceClientCapabilities& c) {}

void from_json(
    const nlohmann::json& j, InlineValueWorkspaceClientCapabilities& c) {}

void to_json(nlohmann::json& j, const InlayHintWorkspaceClientCapabilities& c) {
}

void from_json(
    const nlohmann::json& j, InlayHintWorkspaceClientCapabilities& c) {}

void to_json(
    nlohmann::json& j, const DiagnosticWorkspaceClientCapabilities& c) {}

void from_json(
    const nlohmann::json& j, DiagnosticWorkspaceClientCapabilities& c) {}

void to_json(
    nlohmann::json& j, const WorkspaceClientCapabilities::FileOperations& c) {
  j = nlohmann::json{};
  to_json_optional(j, "dynamicRegistration", c.dynamicRegistration);
  to_json_optional(j, "didCreate", c.didCreate);
  to_json_optional(j, "willCreate", c.willCreate);
  to_json_optional(j, "didRename", c.didRename);
  to_json_optional(j, "willRename", c.willRename);
  to_json_optional(j, "didDelete", c.didDelete);
  to_json_optional(j, "willDelete", c.willDelete);
}

void from_json(
    const nlohmann::json& j, WorkspaceClientCapabilities::FileOperations& c) {
  from_json_optional(j, "dynamicRegistration", c.dynamicRegistration);
  from_json_optional(j, "didCreate", c.didCreate);
  from_json_optional(j, "willCreate", c.willCreate);
  from_json_optional(j, "didRename", c.didRename);
  from_json_optional(j, "willRename", c.willRename);
}

void to_json(nlohmann::json& j, const WorkspaceClientCapabilities& c) {
  j = nlohmann::json{};
  to_json_optional(j, "applyEdit", c.applyEdit);
  to_json_optional(j, "workspaceEdit", c.workspaceEdit);
  to_json_optional(j, "didChangeConfiguration", c.didChangeConfiguration);
  to_json_optional(j, "didChangeWatchedFiles", c.didChangeWatchedFiles);
  to_json_optional(j, "symbol", c.symbol);
  to_json_optional(j, "executeCommand", c.executeCommand);
  to_json_optional(j, "workspaceFolders", c.workspaceFolders);
  to_json_optional(j, "configuration", c.configuration);
  to_json_optional(j, "semanticTokens", c.semanticTokens);
  to_json_optional(j, "codeLens", c.codeLens);
  to_json_optional(j, "fileOperations", c.fileOperations);
  to_json_optional(j, "inlineValue", c.inlineValue);
  to_json_optional(j, "inlayHint", c.inlayHint);
  to_json_optional(j, "diagnostics", c.diagnostics);
}

void from_json(const nlohmann::json& j, WorkspaceClientCapabilities& c) {
  from_json_optional(j, "applyEdit", c.applyEdit);
  from_json_optional(j, "workspaceEdit", c.workspaceEdit);
  from_json_optional(j, "didChangeConfiguration", c.didChangeConfiguration);
  from_json_optional(j, "didChangeWatchedFiles", c.didChangeWatchedFiles);
  from_json_optional(j, "symbol", c.symbol);
  from_json_optional(j, "executeCommand", c.executeCommand);
  from_json_optional(j, "workspaceFolders", c.workspaceFolders);
  from_json_optional(j, "configuration", c.configuration);
  from_json_optional(j, "semanticTokens", c.semanticTokens);
  from_json_optional(j, "codeLens", c.codeLens);
  from_json_optional(j, "fileOperations", c.fileOperations);
}

// Text document specific client capabilities
void to_json(nlohmann::json& j, const TextDocumentSyncClientCapabilities& c) {}

void from_json(const nlohmann::json& j, TextDocumentSyncClientCapabilities& c) {
}

void to_json(nlohmann::json& j, const CompletionClientCapabilities& c) {}

void from_json(const nlohmann::json& j, CompletionClientCapabilities& c) {}

void to_json(nlohmann::json& j, const HoverClientCapabilities& c) {}

void from_json(const nlohmann::json& j, HoverClientCapabilities& c) {}

void to_json(nlohmann::json& j, const SignatureHelpClientCapabilities& c) {}

void from_json(const nlohmann::json& j, SignatureHelpClientCapabilities& c) {}

void to_json(nlohmann::json& j, const DeclarationClientCapabilities& c) {}

void from_json(const nlohmann::json& j, DeclarationClientCapabilities& c) {}

void to_json(nlohmann::json& j, const DefinitionClientCapabilities& c) {}

void from_json(const nlohmann::json& j, DefinitionClientCapabilities& c) {}

void to_json(nlohmann::json& j, const TypeDefinitionClientCapabilities& c) {}

void from_json(const nlohmann::json& j, TypeDefinitionClientCapabilities& c) {}

void to_json(nlohmann::json& j, const ImplementationClientCapabilities& c) {}

void from_json(const nlohmann::json& j, ImplementationClientCapabilities& c) {}

void to_json(nlohmann::json& j, const ReferenceClientCapabilities& c) {}

void from_json(const nlohmann::json& j, ReferenceClientCapabilities& c) {}

void to_json(nlohmann::json& j, const DocumentHighlightClientCapabilities& c) {}

void from_json(
    const nlohmann::json& j, DocumentHighlightClientCapabilities& c) {}

void to_json(nlohmann::json& j, const DocumentSymbolClientCapabilities& c) {}

void from_json(const nlohmann::json& j, DocumentSymbolClientCapabilities& c) {}

void to_json(nlohmann::json& j, const CodeActionClientCapabilities& c) {}

void from_json(const nlohmann::json& j, CodeActionClientCapabilities& c) {}

void to_json(nlohmann::json& j, const CodeLensClientCapabilities& c) {}

void from_json(const nlohmann::json& j, CodeLensClientCapabilities& c) {}

void to_json(nlohmann::json& j, const DocumentLinkClientCapabilities& c) {}

void from_json(const nlohmann::json& j, DocumentLinkClientCapabilities& c) {}

void to_json(nlohmann::json& j, const DocumentColorClientCapabilities& c) {}

void from_json(const nlohmann::json& j, DocumentColorClientCapabilities& c) {}

void to_json(nlohmann::json& j, const DocumentFormattingClientCapabilities& c) {
}

void from_json(
    const nlohmann::json& j, DocumentFormattingClientCapabilities& c) {}

void to_json(
    nlohmann::json& j, const DocumentRangeFormattingClientCapabilities& c) {}

void from_json(
    const nlohmann::json& j, DocumentRangeFormattingClientCapabilities& c) {}

void to_json(
    nlohmann::json& j, const DocumentOnTypeFormattingClientCapabilities& c) {}

void from_json(
    const nlohmann::json& j, DocumentOnTypeFormattingClientCapabilities& c) {}

void to_json(nlohmann::json& j, const RenameClientCapabilities& c) {}

void from_json(const nlohmann::json& j, RenameClientCapabilities& c) {}

void to_json(nlohmann::json& j, const PublishDiagnosticsClientCapabilities& c) {
}

void from_json(
    const nlohmann::json& j, PublishDiagnosticsClientCapabilities& c) {}

void to_json(nlohmann::json& j, const FoldingRangeClientCapabilities& c) {}

void from_json(const nlohmann::json& j, FoldingRangeClientCapabilities& c) {}

void to_json(nlohmann::json& j, const SelectionRangeClientCapabilities& c) {}

void from_json(const nlohmann::json& j, SelectionRangeClientCapabilities& c) {}

void to_json(nlohmann::json& j, const LinkedEditingRangeClientCapabilities& c) {
}

void from_json(
    const nlohmann::json& j, LinkedEditingRangeClientCapabilities& c) {}

void to_json(nlohmann::json& j, const CallHierarchyClientCapabilities& c) {}

void from_json(const nlohmann::json& j, CallHierarchyClientCapabilities& c) {}

void to_json(nlohmann::json& j, const SemanticTokensClientCapabilities& c) {}

void from_json(const nlohmann::json& j, SemanticTokensClientCapabilities& c) {}

void to_json(nlohmann::json& j, const MonikerClientCapabilities& c) {}

void from_json(const nlohmann::json& j, MonikerClientCapabilities& c) {}

void to_json(nlohmann::json& j, const TypeHierarchyClientCapabilities& c) {}

void from_json(const nlohmann::json& j, TypeHierarchyClientCapabilities& c) {}

void to_json(nlohmann::json& j, const InlineValueClientCapabilities& c) {}

void from_json(const nlohmann::json& j, InlineValueClientCapabilities& c) {}

void to_json(nlohmann::json& j, const InlayHintClientCapabilities& c) {}

void from_json(const nlohmann::json& j, InlayHintClientCapabilities& c) {}

void to_json(nlohmann::json& j, const DiagnosticClientCapabilities& c) {}

void from_json(const nlohmann::json& j, DiagnosticClientCapabilities& c) {}

void to_json(nlohmann::json& j, const TextDocumentClientCapabilities& c) {
  j = nlohmann::json{};
  to_json_optional(j, "synchronization", c.synchronization);
  to_json_optional(j, "completion", c.completion);
  to_json_optional(j, "hover", c.hover);
  to_json_optional(j, "signatureHelp", c.signatureHelp);
  to_json_optional(j, "declaration", c.declaration);
  to_json_optional(j, "definition", c.definition);
  to_json_optional(j, "typeDefinition", c.typeDefinition);
  to_json_optional(j, "implementation", c.implementation);
  to_json_optional(j, "references", c.references);
  to_json_optional(j, "documentHighlight", c.documentHighlight);
  to_json_optional(j, "documentSymbol", c.documentSymbol);
  to_json_optional(j, "codeAction", c.codeAction);
  to_json_optional(j, "codeLens", c.codeLens);
  to_json_optional(j, "documentLink", c.documentLink);
  to_json_optional(j, "colorProvider", c.colorProvider);
  to_json_optional(j, "formatting", c.formatting);
  to_json_optional(j, "rangeFormatting", c.rangeFormatting);
  to_json_optional(j, "onTypeFormatting", c.onTypeFormatting);
  to_json_optional(j, "rename", c.rename);
  to_json_optional(j, "publishDiagnostics", c.publishDiagnostics);
  to_json_optional(j, "foldingRange", c.foldingRange);
  to_json_optional(j, "selectionRange", c.selectionRange);
  to_json_optional(j, "linkedEditingRange", c.linkedEditingRange);
  to_json_optional(j, "callHierarchy", c.callHierarchy);
  to_json_optional(j, "semanticTokens", c.semanticTokens);
  to_json_optional(j, "moniker", c.moniker);
  to_json_optional(j, "typeHierarchy", c.typeHierarchy);
  to_json_optional(j, "inlineValue", c.inlineValue);
  to_json_optional(j, "inlayHint", c.inlayHint);
  to_json_optional(j, "diagnostic", c.diagnostic);
}

void from_json(const nlohmann::json& j, TextDocumentClientCapabilities& c) {
  from_json_optional(j, "synchronization", c.synchronization);
  from_json_optional(j, "completion", c.completion);
  from_json_optional(j, "hover", c.hover);
  from_json_optional(j, "signatureHelp", c.signatureHelp);
  from_json_optional(j, "declaration", c.declaration);
  from_json_optional(j, "definition", c.definition);
  from_json_optional(j, "typeDefinition", c.typeDefinition);
  from_json_optional(j, "implementation", c.implementation);
  from_json_optional(j, "references", c.references);
  from_json_optional(j, "documentHighlight", c.documentHighlight);
  from_json_optional(j, "documentSymbol", c.documentSymbol);
  from_json_optional(j, "codeAction", c.codeAction);
  from_json_optional(j, "codeLens", c.codeLens);
  from_json_optional(j, "documentLink", c.documentLink);
  from_json_optional(j, "colorProvider", c.colorProvider);
  from_json_optional(j, "formatting", c.formatting);
  from_json_optional(j, "rangeFormatting", c.rangeFormatting);
  from_json_optional(j, "onTypeFormatting", c.onTypeFormatting);
  from_json_optional(j, "rename", c.rename);
  from_json_optional(j, "publishDiagnostics", c.publishDiagnostics);
  from_json_optional(j, "foldingRange", c.foldingRange);
  from_json_optional(j, "selectionRange", c.selectionRange);
  from_json_optional(j, "linkedEditingRange", c.linkedEditingRange);
  from_json_optional(j, "callHierarchy", c.callHierarchy);
  from_json_optional(j, "semanticTokens", c.semanticTokens);
  from_json_optional(j, "moniker", c.moniker);
  from_json_optional(j, "typeHierarchy", c.typeHierarchy);
  from_json_optional(j, "inlineValue", c.inlineValue);
  from_json_optional(j, "inlayHint", c.inlayHint);
  from_json_optional(j, "diagnostic", c.diagnostic);
}

// Capabilities specific to the notebook document support
void to_json(
    nlohmann::json& j, const NotebookDocumentSyncClientCapabilities& c) {}

void from_json(
    const nlohmann::json& j, NotebookDocumentSyncClientCapabilities& c) {}

void to_json(nlohmann::json& j, const NotebookDocumentClientCapabilities& c) {
  j = nlohmann::json{};
  to_json_optional(j, "synchronization", c.synchronization);
}

void from_json(const nlohmann::json& j, NotebookDocumentClientCapabilities& c) {
  from_json_optional(j, "synchronization", c.synchronization);
}

// Window specific client capabilities
void to_json(nlohmann::json& j, const ShowMessageRequestClientCapabilities& c) {
}

void from_json(
    const nlohmann::json& j, ShowMessageRequestClientCapabilities& c) {}

void to_json(nlohmann::json& j, const ShowDocumentClientCapabilities& c) {}

void from_json(const nlohmann::json& j, ShowDocumentClientCapabilities& c) {}

void to_json(nlohmann::json& j, const WindowClientCapabilities& c) {
  j = nlohmann::json{};
  to_json_optional(j, "workDoneProgress", c.workDoneProgress);
  to_json_optional(j, "showMessage", c.showMessage);
  to_json_optional(j, "showDocument", c.showDocument);
}

void from_json(const nlohmann::json& j, WindowClientCapabilities& c) {
  from_json_optional(j, "workDoneProgress", c.workDoneProgress);
  from_json_optional(j, "showMessage", c.showMessage);
  from_json_optional(j, "showDocument", c.showDocument);
}

// General client capabilities
void to_json(nlohmann::json& j, const StaleRequestSupport& c) {
  j = nlohmann::json{
      {"cancel", c.cancel},
      {"retryOnContentModified", c.retryOnContentModified}};
}

void from_json(const nlohmann::json& j, StaleRequestSupport& c) {
  j.at("cancel").get_to(c.cancel);
  j.at("retryOnContentModified").get_to(c.retryOnContentModified);
}

void to_json(nlohmann::json& j, const GeneralClientCapabilities& c) {
  j = nlohmann::json{};
  to_json_optional(j, "staleRequestSupport", c.staleRequestSupport);
  to_json_optional(j, "regularExpressions", c.regularExpressions);
  to_json_optional(j, "markdown", c.markdown);
  to_json_optional(j, "positionEncodings", c.positionEncodings);
}

void from_json(const nlohmann::json& j, GeneralClientCapabilities& c) {
  from_json_optional(j, "staleRequestSupport", c.staleRequestSupport);
  from_json_optional(j, "regularExpressions", c.regularExpressions);
  from_json_optional(j, "markdown", c.markdown);
  from_json_optional(j, "positionEncodings", c.positionEncodings);
}

void to_json(nlohmann::json& j, const ClientCapabilities& c) {
  j = nlohmann::json{};
  to_json_optional(j, "workspace", c.workspace);
  to_json_optional(j, "textDocument", c.textDocument);
  to_json_optional(j, "notebookDocument", c.notebookDocument);
  to_json_optional(j, "window", c.window);
  to_json_optional(j, "general", c.general);
  to_json_optional(j, "experimental", c.experimental);
}

void from_json(const nlohmann::json& j, ClientCapabilities& c) {
  from_json_optional(j, "workspace", c.workspace);
  from_json_optional(j, "textDocument", c.textDocument);
  from_json_optional(j, "notebookDocument", c.notebookDocument);
  from_json_optional(j, "window", c.window);
  from_json_optional(j, "general", c.general);
  from_json_optional(j, "experimental", c.experimental);
}

}  // namespace lsp

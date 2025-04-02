#include "lsp/server_capabilities.hpp"

#include "lsp/json_utils.hpp"

namespace lsp {

void to_json(nlohmann::json& j, const TextDocumentSyncKind& o) {
  j = nlohmann::json(static_cast<int>(o));
};

void from_json(const nlohmann::json& j, TextDocumentSyncKind& o) {
  o = static_cast<TextDocumentSyncKind>(j.get<int>());
};

void to_json(nlohmann::json& j, const SaveOptions& o) {
  j = nlohmann::json{};
  to_json_optional(j, "includeText", o.includeText);
};

void from_json(const nlohmann::json& j, SaveOptions& o) {
  from_json_optional(j, "includeText", o.includeText);
};

void to_json(nlohmann::json& j, const TextDocumentSyncOptions::Save& o) {
  std::visit([&j](auto&& arg) { to_json(j, arg); }, o);
};

void from_json(const nlohmann::json& j, TextDocumentSyncOptions::Save& o) {
  if (j.is_boolean()) {
    o = j.get<bool>();
  } else {
    o = j.get<SaveOptions>();
  }
};

void to_json(nlohmann::json& j, const TextDocumentSyncOptions& o) {
  j = nlohmann::json{};
  to_json_optional(j, "openClose", o.openClose);
  to_json_optional(j, "change", o.change);
  to_json_optional(j, "willSave", o.willSave);
  to_json_optional(j, "willSaveWaitUntil", o.willSaveWaitUntil);
  to_json_optional(j, "save", o.save);
};

void from_json(const nlohmann::json& j, TextDocumentSyncOptions& o) {
  from_json_optional(j, "openClose", o.openClose);
  from_json_optional(j, "change", o.change);
  from_json_optional(j, "willSave", o.willSave);
  from_json_optional(j, "willSaveWaitUntil", o.willSaveWaitUntil);
  from_json_optional(j, "save", o.save);
};

void to_json(nlohmann::json& j, const NotebookDocumentSyncOptions& o) {};

void from_json(const nlohmann::json& j, NotebookDocumentSyncOptions& o) {};

void to_json(
    nlohmann::json& j, const NotebookDocumentSyncRegistrationOptions& o) {};

void from_json(
    const nlohmann::json& j, NotebookDocumentSyncRegistrationOptions& o) {};

void to_json(nlohmann::json& j, const CompletionOptions& o) {};

void from_json(const nlohmann::json& j, CompletionOptions& o) {};

void to_json(nlohmann::json& j, const HoverOptions& o) {};

void from_json(const nlohmann::json& j, HoverOptions& o) {};

void to_json(nlohmann::json& j, const SignatureHelpOptions& o) {};

void from_json(const nlohmann::json& j, SignatureHelpOptions& o) {};

void to_json(nlohmann::json& j, const DeclarationOptions& o) {};

void from_json(const nlohmann::json& j, DeclarationOptions& o) {};

void to_json(nlohmann::json& j, const DeclarationRegistrationOptions& o) {};

void from_json(const nlohmann::json& j, DeclarationRegistrationOptions& o) {};

void to_json(nlohmann::json& j, const DefinitionOptions& o) {};

void from_json(const nlohmann::json& j, DefinitionOptions& o) {};

void to_json(nlohmann::json& j, const TypeDefinitionOptions& o) {};

void from_json(const nlohmann::json& j, TypeDefinitionOptions& o) {};

void to_json(nlohmann::json& j, const TypeDefinitionRegistrationOptions& o) {};

void from_json(const nlohmann::json& j, TypeDefinitionRegistrationOptions& o) {
};

void to_json(nlohmann::json& j, const ImplementationOptions& o) {};

void from_json(const nlohmann::json& j, ImplementationOptions& o) {};

void to_json(nlohmann::json& j, const ImplementationRegistrationOptions& o) {};

void from_json(const nlohmann::json& j, ImplementationRegistrationOptions& o) {
};

void to_json(nlohmann::json& j, const ReferenceOptions& o) {};

void from_json(const nlohmann::json& j, ReferenceOptions& o) {};

void to_json(nlohmann::json& j, const DocumentHighlightOptions& o) {};

void from_json(const nlohmann::json& j, DocumentHighlightOptions& o) {};

void to_json(nlohmann::json& j, const DocumentSymbolOptions& o) {};

void from_json(const nlohmann::json& j, DocumentSymbolOptions& o) {};

void to_json(nlohmann::json& j, const CodeActionOptions& o) {};

void from_json(const nlohmann::json& j, CodeActionOptions& o) {};

void to_json(nlohmann::json& j, const CodeLensOptions& o) {};

void from_json(const nlohmann::json& j, CodeLensOptions& o) {};

void to_json(nlohmann::json& j, const DocumentLinkOptions& o) {};

void from_json(const nlohmann::json& j, DocumentLinkOptions& o) {};

void to_json(nlohmann::json& j, const DocumentColorOptions& o) {};

void from_json(const nlohmann::json& j, DocumentColorOptions& o) {};

void to_json(nlohmann::json& j, const DocumentColorRegistrationOptions& o) {};

void from_json(const nlohmann::json& j, DocumentColorRegistrationOptions& o) {};

void to_json(nlohmann::json& j, const DocumentFormattingOptions& o) {};

void from_json(const nlohmann::json& j, DocumentFormattingOptions& o) {};

void to_json(nlohmann::json& j, const DocumentRangeFormattingOptions& o) {};

void from_json(const nlohmann::json& j, DocumentRangeFormattingOptions& o) {};

void to_json(nlohmann::json& j, const DocumentOnTypeFormattingOptions& o) {};

void from_json(const nlohmann::json& j, DocumentOnTypeFormattingOptions& o) {};

void to_json(nlohmann::json& j, const RenameOptions& o) {};

void from_json(const nlohmann::json& j, RenameOptions& o) {};

void to_json(nlohmann::json& j, const FoldingRangeOptions& o) {};

void from_json(const nlohmann::json& j, FoldingRangeOptions& o) {};

void to_json(nlohmann::json& j, const FoldingRangeRegistrationOptions& o) {};

void from_json(const nlohmann::json& j, FoldingRangeRegistrationOptions& o) {};

void to_json(nlohmann::json& j, const ExecuteCommandOptions& o) {};

void from_json(const nlohmann::json& j, ExecuteCommandOptions& o) {};

void to_json(nlohmann::json& j, const SelectionRangeOptions& o) {};

void from_json(const nlohmann::json& j, SelectionRangeOptions& o) {};

void to_json(nlohmann::json& j, const SelectionRangeRegistrationOptions& o) {};

void from_json(const nlohmann::json& j, SelectionRangeRegistrationOptions& o) {
};

void to_json(nlohmann::json& j, const LinkedEditingRangeOptions& o) {};

void from_json(const nlohmann::json& j, LinkedEditingRangeOptions& o) {};

void to_json(
    nlohmann::json& j, const LinkedEditingRangeRegistrationOptions& o) {};

void from_json(
    const nlohmann::json& j, LinkedEditingRangeRegistrationOptions& o) {};

void to_json(nlohmann::json& j, const CallHierarchyOptions& o) {};

void from_json(const nlohmann::json& j, CallHierarchyOptions& o) {};

void to_json(nlohmann::json& j, const CallHierarchyRegistrationOptions& o) {};

void from_json(const nlohmann::json& j, CallHierarchyRegistrationOptions& o) {};

void to_json(nlohmann::json& j, const SemanticTokensOptions& o) {};

void from_json(const nlohmann::json& j, SemanticTokensOptions& o) {};

void to_json(nlohmann::json& j, const SemanticTokensRegistrationOptions& o) {};

void from_json(const nlohmann::json& j, SemanticTokensRegistrationOptions& o) {
};

void to_json(nlohmann::json& j, const MonikerOptions& o) {};

void from_json(const nlohmann::json& j, MonikerOptions& o) {};

void to_json(nlohmann::json& j, const MonikerRegistrationOptions& o) {};

void from_json(const nlohmann::json& j, MonikerRegistrationOptions& o) {};

void to_json(nlohmann::json& j, const TypeHierarchyOptions& o) {};

void from_json(const nlohmann::json& j, TypeHierarchyOptions& o) {};

void to_json(nlohmann::json& j, const TypeHierarchyRegistrationOptions& o) {};

void from_json(const nlohmann::json& j, TypeHierarchyRegistrationOptions& o) {};

void to_json(nlohmann::json& j, const InlineValueOptions& o) {};

void from_json(const nlohmann::json& j, InlineValueOptions& o) {};

void to_json(nlohmann::json& j, const InlineValueRegistrationOptions& o) {};

void from_json(const nlohmann::json& j, InlineValueRegistrationOptions& o) {};

void to_json(nlohmann::json& j, const InlayHintOptions& o) {};

void from_json(const nlohmann::json& j, InlayHintOptions& o) {};

void to_json(nlohmann::json& j, const InlayHintRegistrationOptions& o) {};

void from_json(const nlohmann::json& j, InlayHintRegistrationOptions& o) {};

void to_json(nlohmann::json& j, const DiagnosticOptions& o) {};

void from_json(const nlohmann::json& j, DiagnosticOptions& o) {};

void to_json(nlohmann::json& j, const DiagnosticRegistrationOptions& o) {};

void from_json(const nlohmann::json& j, DiagnosticRegistrationOptions& o) {};

void to_json(nlohmann::json& j, const WorkspaceSymbolOptions& o) {};

void from_json(const nlohmann::json& j, WorkspaceSymbolOptions& o) {};

void to_json(nlohmann::json& j, const WorkspaceFoldersServerCapabilities& o) {};

void from_json(const nlohmann::json& j, WorkspaceFoldersServerCapabilities& o) {
};

void to_json(nlohmann::json& j, const FileOperationRegistrationOptions& o) {};

void from_json(const nlohmann::json& j, FileOperationRegistrationOptions& o) {};

void to_json(nlohmann::json& j, const ServerCapabilities::TextDocumentSync& o) {
  std::visit([&j](auto&& arg) { to_json(j, arg); }, o);
};

void from_json(
    const nlohmann::json& j, ServerCapabilities::TextDocumentSync& o) {
  if (j.contains("openClose")) {
    o = j.get<TextDocumentSyncOptions>();
  } else {
    o = j.get<TextDocumentSyncKind>();
  }
}

void to_json(
    nlohmann::json& j, const ServerCapabilities::NotebookDocumentSync& o) {
  std::visit([&j](auto&& arg) { to_json(j, arg); }, o);
};

void from_json(
    const nlohmann::json& j, ServerCapabilities::NotebookDocumentSync& o) {
  if (j.contains("id")) {
    o = j.get<NotebookDocumentSyncRegistrationOptions>();
  } else {
    o = j.get<NotebookDocumentSyncOptions>();
  }
}

void to_json(nlohmann::json& j, const ServerCapabilities::HoverProvider& o) {
  std::visit([&j](auto&& arg) { to_json(j, arg); }, o);
};

void from_json(const nlohmann::json& j, ServerCapabilities::HoverProvider& o) {
  if (j.is_boolean()) {
    o = j.get<bool>();
  } else {
    o = j.get<HoverOptions>();
  }
};

void to_json(
    nlohmann::json& j, const ServerCapabilities::DeclarationProvider& o) {
  std::visit([&j](auto&& arg) { to_json(j, arg); }, o);
};

void from_json(
    const nlohmann::json& j, ServerCapabilities::DeclarationProvider& o) {
  if (j.is_boolean()) {
    o = j.get<bool>();
  } else if (j.contains("documentSelector")) {
    o = j.get<DeclarationRegistrationOptions>();
  } else {
    o = j.get<DeclarationOptions>();
  }
}

void to_json(
    nlohmann::json& j, const ServerCapabilities::DefinitionProvider& o) {
  std::visit([&j](auto&& arg) { to_json(j, arg); }, o);
};

void from_json(
    const nlohmann::json& j, ServerCapabilities::DefinitionProvider& o) {
  if (j.is_boolean()) {
    o = j.get<bool>();
  } else {
    o = j.get<DefinitionOptions>();
  }
}

void to_json(
    nlohmann::json& j, const ServerCapabilities::TypeDefinitionProvider& o) {
  std::visit([&j](auto&& arg) { to_json(j, arg); }, o);
};

void from_json(
    const nlohmann::json& j, ServerCapabilities::TypeDefinitionProvider& o) {
  if (j.is_boolean()) {
    o = j.get<bool>();
  } else if (j.contains("documentSelector")) {
    o = j.get<TypeDefinitionRegistrationOptions>();
  } else {
    o = j.get<TypeDefinitionOptions>();
  }
}

void to_json(
    nlohmann::json& j, const ServerCapabilities::ImplementationProvider& o) {
  std::visit([&j](auto&& arg) { to_json(j, arg); }, o);
}

void from_json(
    const nlohmann::json& j, ServerCapabilities::ImplementationProvider& o) {
  if (j.is_boolean()) {
    o = j.get<bool>();
  } else if (j.contains("documentSelector")) {
    o = j.get<ImplementationRegistrationOptions>();
  } else {
    o = j.get<ImplementationOptions>();
  }
}

void to_json(
    nlohmann::json& j, const ServerCapabilities::ReferencesProvider& o) {
  std::visit([&j](auto&& arg) { to_json(j, arg); }, o);
}

void from_json(
    const nlohmann::json& j, ServerCapabilities::ReferencesProvider& o) {
  if (j.is_boolean()) {
    o = j.get<bool>();
  } else {
    o = j.get<ReferenceOptions>();
  }
}

void to_json(
    nlohmann::json& j, const ServerCapabilities::DocumentHighlightProvider& o) {
  std::visit([&j](auto&& arg) { to_json(j, arg); }, o);
}

void from_json(
    const nlohmann::json& j, ServerCapabilities::DocumentHighlightProvider& o) {
  if (j.is_boolean()) {
    o = j.get<bool>();
  } else {
    o = j.get<DocumentHighlightOptions>();
  }
}

void to_json(
    nlohmann::json& j, const ServerCapabilities::DocumentSymbolProvider& o) {
  std::visit([&j](auto&& arg) { to_json(j, arg); }, o);
}

void from_json(
    const nlohmann::json& j, ServerCapabilities::DocumentSymbolProvider& o) {
  if (j.is_boolean()) {
    o = j.get<bool>();
  } else {
    o = j.get<DocumentSymbolOptions>();
  }
}

void to_json(
    nlohmann::json& j, const ServerCapabilities::CodeActionProvider& o) {
  std::visit([&j](auto&& arg) { to_json(j, arg); }, o);
}

void from_json(
    const nlohmann::json& j, ServerCapabilities::CodeActionProvider& o) {
  if (j.is_boolean()) {
    o = j.get<bool>();
  } else {
    o = j.get<CodeActionOptions>();
  }
}

void to_json(nlohmann::json& j, const ServerCapabilities::ColorProvider& o) {
  std::visit([&j](auto&& arg) { to_json(j, arg); }, o);
}

void from_json(const nlohmann::json& j, ServerCapabilities::ColorProvider& o) {
  if (j.is_boolean()) {
    o = j.get<bool>();
  } else if (j.contains("documentSelector")) {
    o = j.get<DocumentColorRegistrationOptions>();
  } else {
    o = j.get<DocumentColorOptions>();
  }
}

void to_json(
    nlohmann::json& j,
    const ServerCapabilities::DocumentFormattingProvider& o) {
  std::visit([&j](auto&& arg) { to_json(j, arg); }, o);
}

void from_json(
    const nlohmann::json& j,
    ServerCapabilities::DocumentFormattingProvider& o) {
  if (j.is_boolean()) {
    o = j.get<bool>();
  } else {
    o = j.get<DocumentFormattingOptions>();
  }
}

void to_json(
    nlohmann::json& j,
    const ServerCapabilities::DocumentRangeFormattingProvider& o) {
  std::visit([&j](auto&& arg) { to_json(j, arg); }, o);
}

void from_json(
    const nlohmann::json& j,
    ServerCapabilities::DocumentRangeFormattingProvider& o) {
  if (j.is_boolean()) {
    o = j.get<bool>();
  } else {
    o = j.get<DocumentRangeFormattingOptions>();
  }
}

void to_json(nlohmann::json& j, const ServerCapabilities::RenameProvider& o) {
  std::visit([&j](auto&& arg) { to_json(j, arg); }, o);
}

void from_json(const nlohmann::json& j, ServerCapabilities::RenameProvider& o) {
  if (j.is_boolean()) {
    o = j.get<bool>();
  } else {
    o = j.get<RenameOptions>();
  }
}

void to_json(
    nlohmann::json& j, const ServerCapabilities::FoldingRangeProvider& o) {
  std::visit([&j](auto&& arg) { to_json(j, arg); }, o);
}

void from_json(
    const nlohmann::json& j, ServerCapabilities::FoldingRangeProvider& o) {
  if (j.is_boolean()) {
    o = j.get<bool>();
  } else if (j.contains("documentSelector")) {
    o = j.get<FoldingRangeRegistrationOptions>();
  } else {
    o = j.get<FoldingRangeOptions>();
  }
}

void to_json(
    nlohmann::json& j, const ServerCapabilities::SelectionRangeProvider& o) {
  std::visit([&j](auto&& arg) { to_json(j, arg); }, o);
}

void from_json(
    const nlohmann::json& j, ServerCapabilities::SelectionRangeProvider& o) {
  if (j.is_boolean()) {
    o = j.get<bool>();
  } else if (j.contains("documentSelector")) {
    o = j.get<SelectionRangeRegistrationOptions>();
  } else {
    o = j.get<SelectionRangeOptions>();
  }
}

void to_json(
    nlohmann::json& j,
    const ServerCapabilities::LinkedEditingRangeProvider& o) {
  std::visit([&j](auto&& arg) { to_json(j, arg); }, o);
}

void from_json(
    const nlohmann::json& j,
    ServerCapabilities::LinkedEditingRangeProvider& o) {
  if (j.is_boolean()) {
    o = j.get<bool>();
  } else if (j.contains("documentSelector")) {
    o = j.get<LinkedEditingRangeRegistrationOptions>();
  } else {
    o = j.get<LinkedEditingRangeOptions>();
  }
}

void to_json(
    nlohmann::json& j, const ServerCapabilities::CallHierarchyProvider& o) {
  std::visit([&j](auto&& arg) { to_json(j, arg); }, o);
}

void from_json(
    const nlohmann::json& j, ServerCapabilities::CallHierarchyProvider& o) {
  if (j.is_boolean()) {
    o = j.get<bool>();
  } else if (j.contains("documentSelector")) {
    o = j.get<CallHierarchyRegistrationOptions>();
  } else {
    o = j.get<CallHierarchyOptions>();
  }
}

void to_json(
    nlohmann::json& j, const ServerCapabilities::SemanticTokensProvider& o) {
  std::visit([&j](auto&& arg) { to_json(j, arg); }, o);
}

void from_json(
    const nlohmann::json& j, ServerCapabilities::SemanticTokensProvider& o) {
  if (j.is_boolean()) {
    o = j.get<bool>();
  } else if (j.contains("documentSelector")) {
    o = j.get<SemanticTokensRegistrationOptions>();
  } else {
    o = j.get<SemanticTokensOptions>();
  }
}

void to_json(nlohmann::json& j, const ServerCapabilities::MonikerProvider& o) {
  std::visit([&j](auto&& arg) { to_json(j, arg); }, o);
}

void from_json(
    const nlohmann::json& j, ServerCapabilities::MonikerProvider& o) {
  if (j.is_boolean()) {
    o = j.get<bool>();
  } else if (j.contains("documentSelector")) {
    o = j.get<MonikerRegistrationOptions>();
  } else {
    o = j.get<MonikerOptions>();
  }
}

void to_json(
    nlohmann::json& j, const ServerCapabilities::TypeHierarchyProvider& o) {
  std::visit([&j](auto&& arg) { to_json(j, arg); }, o);
}

void from_json(
    const nlohmann::json& j, ServerCapabilities::TypeHierarchyProvider& o) {
  if (j.is_boolean()) {
    o = j.get<bool>();
  } else if (j.contains("documentSelector")) {
    o = j.get<TypeHierarchyRegistrationOptions>();
  } else {
    o = j.get<TypeHierarchyOptions>();
  }
}

void to_json(
    nlohmann::json& j, const ServerCapabilities::InlineValueProvider& o) {
  std::visit([&j](auto&& arg) { to_json(j, arg); }, o);
}

void from_json(
    const nlohmann::json& j, ServerCapabilities::InlineValueProvider& o) {
  if (j.is_boolean()) {
    o = j.get<bool>();
  } else if (j.contains("documentSelector")) {
    o = j.get<InlineValueRegistrationOptions>();
  } else {
    o = j.get<InlineValueOptions>();
  }
}

void to_json(
    nlohmann::json& j, const ServerCapabilities::InlayHintProvider& o) {
  std::visit([&j](auto&& arg) { to_json(j, arg); }, o);
}

void from_json(
    const nlohmann::json& j, ServerCapabilities::InlayHintProvider& o) {
  if (j.is_boolean()) {
    o = j.get<bool>();
  } else if (j.contains("documentSelector")) {
    o = j.get<InlayHintRegistrationOptions>();
  } else {
    o = j.get<InlayHintOptions>();
  }
}

void to_json(
    nlohmann::json& j, const ServerCapabilities::DiagnosticProvider& o) {
  std::visit([&j](auto&& arg) { to_json(j, arg); }, o);
}

void from_json(
    const nlohmann::json& j, ServerCapabilities::DiagnosticProvider& o) {
  if (j.is_boolean()) {
    o = j.get<bool>();
  } else if (j.contains("documentSelector")) {
    o = j.get<DiagnosticRegistrationOptions>();
  } else {
    o = j.get<DiagnosticOptions>();
  }
}

void to_json(
    nlohmann::json& j, const ServerCapabilities::WorkspaceSymbolProvider& o) {
  std::visit([&j](auto&& arg) { to_json(j, arg); }, o);
}

void from_json(
    const nlohmann::json& j, ServerCapabilities::WorkspaceSymbolProvider& o) {
  if (j.is_boolean()) {
    o = j.get<bool>();
  } else {
    o = j.get<WorkspaceSymbolOptions>();
  }
}

void to_json(nlohmann::json& j, const ServerCapabilities::FileOperations& o) {
  j = nlohmann::json{};
  to_json_optional(j, "didCreate", o.didCreate);
  to_json_optional(j, "willCreate", o.willCreate);
  to_json_optional(j, "didRename", o.didRename);
  to_json_optional(j, "willRename", o.willRename);
  to_json_optional(j, "didDelete", o.didDelete);
  to_json_optional(j, "willDelete", o.willDelete);
};

void from_json(const nlohmann::json& j, ServerCapabilities::FileOperations& o) {
  from_json_optional(j, "didCreate", o.didCreate);
  from_json_optional(j, "willCreate", o.willCreate);
  from_json_optional(j, "didRename", o.didRename);
  from_json_optional(j, "willRename", o.willRename);
  from_json_optional(j, "didDelete", o.didDelete);
  from_json_optional(j, "willDelete", o.willDelete);
};

void to_json(nlohmann::json& j, const ServerCapabilities::Workspace& o) {
  j = nlohmann::json{};
  to_json_optional(j, "workspaceFolders", o.workspaceFolders);
  to_json_optional(j, "fileOperations", o.fileOperations);
};

void from_json(const nlohmann::json& j, ServerCapabilities::Workspace& o) {
  from_json_optional(j, "workspaceFolders", o.workspaceFolders);
  from_json_optional(j, "fileOperations", o.fileOperations);
};

void to_json(nlohmann::json& j, const ServerCapabilities& o) {
  j = nlohmann::json{};
  to_json_optional(j, "positionEncoding", o.positionEncoding);
  to_json_optional(j, "textDocumentSync", o.textDocumentSync);
  to_json_optional(j, "notebookDocumentSync", o.notebookDocumentSync);
  to_json_optional(j, "completionProvider", o.completionProvider);
  to_json_optional(j, "hoverProvider", o.hoverProvider);
  to_json_optional(j, "signatureHelpProvider", o.signatureHelpProvider);
  to_json_optional(j, "declarationProvider", o.declarationProvider);
  to_json_optional(j, "definitionProvider", o.definitionProvider);
  to_json_optional(j, "typeDefinitionProvider", o.typeDefinitionProvider);
  to_json_optional(j, "implementationProvider", o.implementationProvider);
  to_json_optional(j, "referencesProvider", o.referencesProvider);
  to_json_optional(j, "documentHighlightProvider", o.documentHighlightProvider);
  to_json_optional(j, "documentSymbolProvider", o.documentSymbolProvider);
  to_json_optional(j, "codeActionProvider", o.codeActionProvider);
  to_json_optional(j, "codeLensProvider", o.codeLensProvider);
  to_json_optional(j, "documentLinkProvider", o.documentLinkProvider);
  to_json_optional(j, "colorProvider", o.colorProvider);
  to_json_optional(
      j, "documentFormattingProvider", o.documentFormattingProvider);
  to_json_optional(
      j, "documentRangeFormattingProvider", o.documentRangeFormattingProvider);
  to_json_optional(
      j, "documentOnTypeFormattingProvider",
      o.documentOnTypeFormattingProvider);
  to_json_optional(j, "renameProvider", o.renameProvider);
  to_json_optional(j, "foldingRangeProvider", o.foldingRangeProvider);
  to_json_optional(j, "executeCommandProvider", o.executeCommandProvider);
  to_json_optional(j, "selectionRangeProvider", o.selectionRangeProvider);
  to_json_optional(
      j, "linkedEditingRangeProvider", o.linkedEditingRangeProvider);
  to_json_optional(j, "callHierarchyProvider", o.callHierarchyProvider);
  to_json_optional(j, "semanticTokensProvider", o.semanticTokensProvider);
  to_json_optional(j, "monikerProvider", o.monikerProvider);
  to_json_optional(j, "typeHierarchyProvider", o.typeHierarchyProvider);
  to_json_optional(j, "inlineValueProvider", o.inlineValueProvider);
  to_json_optional(j, "inlayHintProvider", o.inlayHintProvider);
  to_json_optional(j, "diagnosticProvider", o.diagnosticProvider);
  to_json_optional(j, "workspaceSymbolProvider", o.workspaceSymbolProvider);
  to_json_optional(j, "workspace", o.workspace);
  to_json_optional(j, "experimental", o.experimental);
};

void from_json(const nlohmann::json& j, ServerCapabilities& o) {
  from_json_optional(j, "positionEncoding", o.positionEncoding);
  from_json_optional(j, "textDocumentSync", o.textDocumentSync);
  from_json_optional(j, "notebookDocumentSync", o.notebookDocumentSync);
  from_json_optional(j, "completionProvider", o.completionProvider);
  from_json_optional(j, "hoverProvider", o.hoverProvider);
  from_json_optional(j, "signatureHelpProvider", o.signatureHelpProvider);
  from_json_optional(j, "declarationProvider", o.declarationProvider);
  from_json_optional(j, "definitionProvider", o.definitionProvider);
  from_json_optional(j, "typeDefinitionProvider", o.typeDefinitionProvider);
  from_json_optional(j, "implementationProvider", o.implementationProvider);
  from_json_optional(j, "referencesProvider", o.referencesProvider);
  from_json_optional(
      j, "documentHighlightProvider", o.documentHighlightProvider);
  from_json_optional(j, "documentSymbolProvider", o.documentSymbolProvider);
  from_json_optional(j, "codeActionProvider", o.codeActionProvider);
  from_json_optional(j, "codeLensProvider", o.codeLensProvider);
  from_json_optional(j, "documentLinkProvider", o.documentLinkProvider);
  from_json_optional(j, "colorProvider", o.colorProvider);
  from_json_optional(
      j, "documentFormattingProvider", o.documentFormattingProvider);
  from_json_optional(
      j, "documentRangeFormattingProvider", o.documentRangeFormattingProvider);
  from_json_optional(
      j, "documentOnTypeFormattingProvider",
      o.documentOnTypeFormattingProvider);
  from_json_optional(j, "renameProvider", o.renameProvider);
  from_json_optional(j, "foldingRangeProvider", o.foldingRangeProvider);
  from_json_optional(j, "executeCommandProvider", o.executeCommandProvider);
  from_json_optional(j, "selectionRangeProvider", o.selectionRangeProvider);
  from_json_optional(
      j, "linkedEditingRangeProvider", o.linkedEditingRangeProvider);
  from_json_optional(j, "callHierarchyProvider", o.callHierarchyProvider);
  from_json_optional(j, "semanticTokensProvider", o.semanticTokensProvider);
  from_json_optional(j, "monikerProvider", o.monikerProvider);
  from_json_optional(j, "typeHierarchyProvider", o.typeHierarchyProvider);
  from_json_optional(j, "inlineValueProvider", o.inlineValueProvider);
  from_json_optional(j, "inlayHintProvider", o.inlayHintProvider);
  from_json_optional(j, "diagnosticProvider", o.diagnosticProvider);
  from_json_optional(j, "workspaceSymbolProvider", o.workspaceSymbolProvider);
  from_json_optional(j, "workspace", o.workspace);
  from_json_optional(j, "experimental", o.experimental);
};

}  // namespace lsp

#pragma once

#include <optional>
#include <variant>

#include <nlohmann/json.hpp>

#include "lsp/basic.hpp"

namespace lsp {

enum class TextDocumentSyncKind {
  None = 0,
  Full = 1,
  Incremental = 2,
};

void to_json(nlohmann::json& j, const TextDocumentSyncKind& o);
void from_json(const nlohmann::json& j, TextDocumentSyncKind& o);

struct SaveOptions {
  std::optional<bool> includeText;
};

void to_json(nlohmann::json& j, const SaveOptions& o);
void from_json(const nlohmann::json& j, SaveOptions& o);

struct TextDocumentSyncOptions {
  std::optional<bool> openClose = true;
  std::optional<TextDocumentSyncKind> change = TextDocumentSyncKind::Full;
  std::optional<bool> willSave = false;
  std::optional<bool> willSaveWaitUntil = false;
  using Save = std::variant<bool, SaveOptions>;
  std::optional<Save> save = true;
};

void to_json(nlohmann::json& j, const TextDocumentSyncOptions::Save& o);
void from_json(const nlohmann::json& j, TextDocumentSyncOptions::Save& o);

void to_json(nlohmann::json& j, const TextDocumentSyncOptions& o);
void from_json(const nlohmann::json& j, TextDocumentSyncOptions& o);

struct NotebookDocumentSyncOptions {};

void to_json(nlohmann::json& j, const NotebookDocumentSyncOptions& o);
void from_json(const nlohmann::json& j, NotebookDocumentSyncOptions& o);

struct NotebookDocumentSyncRegistrationOptions {};

void to_json(
    nlohmann::json& j, const NotebookDocumentSyncRegistrationOptions& o);
void from_json(
    const nlohmann::json& j, NotebookDocumentSyncRegistrationOptions& o);

struct CompletionOptions {};

void to_json(nlohmann::json& j, const CompletionOptions& o);
void from_json(const nlohmann::json& j, CompletionOptions& o);

struct HoverOptions {};

void to_json(nlohmann::json& j, const HoverOptions& o);
void from_json(const nlohmann::json& j, HoverOptions& o);

struct SignatureHelpOptions {};

void to_json(nlohmann::json& j, const SignatureHelpOptions& o);
void from_json(const nlohmann::json& j, SignatureHelpOptions& o);

struct DeclarationOptions {};

void to_json(nlohmann::json& j, const DeclarationOptions& o);
void from_json(const nlohmann::json& j, DeclarationOptions& o);

struct DeclarationRegistrationOptions {};

void to_json(nlohmann::json& j, const DeclarationRegistrationOptions& o);
void from_json(const nlohmann::json& j, DeclarationRegistrationOptions& o);

struct DefinitionOptions {};

void to_json(nlohmann::json& j, const DefinitionOptions& o);
void from_json(const nlohmann::json& j, DefinitionOptions& o);

struct TypeDefinitionOptions {};

void to_json(nlohmann::json& j, const TypeDefinitionOptions& o);
void from_json(const nlohmann::json& j, TypeDefinitionOptions& o);

struct TypeDefinitionRegistrationOptions {};

void to_json(nlohmann::json& j, const TypeDefinitionRegistrationOptions& o);
void from_json(const nlohmann::json& j, TypeDefinitionRegistrationOptions& o);

struct ImplementationOptions {};

void to_json(nlohmann::json& j, const ImplementationOptions& o);
void from_json(const nlohmann::json& j, ImplementationOptions& o);

struct ImplementationRegistrationOptions {};

void to_json(nlohmann::json& j, const ImplementationRegistrationOptions& o);
void from_json(const nlohmann::json& j, ImplementationRegistrationOptions& o);

struct ReferenceOptions {};

void to_json(nlohmann::json& j, const ReferenceOptions& o);
void from_json(const nlohmann::json& j, ReferenceOptions& o);

struct DocumentHighlightOptions {};

void to_json(nlohmann::json& j, const DocumentHighlightOptions& o);
void from_json(const nlohmann::json& j, DocumentHighlightOptions& o);

struct DocumentSymbolOptions {};

void to_json(nlohmann::json& j, const DocumentSymbolOptions& o);
void from_json(const nlohmann::json& j, DocumentSymbolOptions& o);

struct CodeActionOptions {};

void to_json(nlohmann::json& j, const CodeActionOptions& o);
void from_json(const nlohmann::json& j, CodeActionOptions& o);

struct CodeLensOptions {};

void to_json(nlohmann::json& j, const CodeLensOptions& o);
void from_json(const nlohmann::json& j, CodeLensOptions& o);

struct DocumentLinkOptions {};

void to_json(nlohmann::json& j, const DocumentLinkOptions& o);
void from_json(const nlohmann::json& j, DocumentLinkOptions& o);

struct DocumentColorOptions {};

void to_json(nlohmann::json& j, const DocumentColorOptions& o);
void from_json(const nlohmann::json& j, DocumentColorOptions& o);

struct DocumentColorRegistrationOptions {};

void to_json(nlohmann::json& j, const DocumentColorRegistrationOptions& o);
void from_json(const nlohmann::json& j, DocumentColorRegistrationOptions& o);

struct DocumentFormattingOptions {};

void to_json(nlohmann::json& j, const DocumentFormattingOptions& o);
void from_json(const nlohmann::json& j, DocumentFormattingOptions& o);

struct DocumentRangeFormattingOptions {};

void to_json(nlohmann::json& j, const DocumentRangeFormattingOptions& o);
void from_json(const nlohmann::json& j, DocumentRangeFormattingOptions& o);

struct DocumentOnTypeFormattingOptions {};

void to_json(nlohmann::json& j, const DocumentOnTypeFormattingOptions& o);
void from_json(const nlohmann::json& j, DocumentOnTypeFormattingOptions& o);

struct RenameOptions {};

void to_json(nlohmann::json& j, const RenameOptions& o);
void from_json(const nlohmann::json& j, RenameOptions& o);

struct FoldingRangeOptions {};

void to_json(nlohmann::json& j, const FoldingRangeOptions& o);
void from_json(const nlohmann::json& j, FoldingRangeOptions& o);

struct FoldingRangeRegistrationOptions {};

void to_json(nlohmann::json& j, const FoldingRangeRegistrationOptions& o);
void from_json(const nlohmann::json& j, FoldingRangeRegistrationOptions& o);

struct ExecuteCommandOptions {};

void to_json(nlohmann::json& j, const ExecuteCommandOptions& o);
void from_json(const nlohmann::json& j, ExecuteCommandOptions& o);

struct SelectionRangeOptions {};

void to_json(nlohmann::json& j, const SelectionRangeOptions& o);
void from_json(const nlohmann::json& j, SelectionRangeOptions& o);

struct SelectionRangeRegistrationOptions {};

void to_json(nlohmann::json& j, const SelectionRangeRegistrationOptions& o);
void from_json(const nlohmann::json& j, SelectionRangeRegistrationOptions& o);

struct LinkedEditingRangeOptions {};

void to_json(nlohmann::json& j, const LinkedEditingRangeOptions& o);
void from_json(const nlohmann::json& j, LinkedEditingRangeOptions& o);

struct LinkedEditingRangeRegistrationOptions {};

void to_json(nlohmann::json& j, const LinkedEditingRangeRegistrationOptions& o);
void from_json(
    const nlohmann::json& j, LinkedEditingRangeRegistrationOptions& o);

struct CallHierarchyOptions {};

void to_json(nlohmann::json& j, const CallHierarchyOptions& o);
void from_json(const nlohmann::json& j, CallHierarchyOptions& o);

struct CallHierarchyRegistrationOptions {};

void to_json(nlohmann::json& j, const CallHierarchyRegistrationOptions& o);
void from_json(const nlohmann::json& j, CallHierarchyRegistrationOptions& o);

struct SemanticTokensOptions {};

void to_json(nlohmann::json& j, const SemanticTokensOptions& o);
void from_json(const nlohmann::json& j, SemanticTokensOptions& o);

struct SemanticTokensRegistrationOptions {};

void to_json(nlohmann::json& j, const SemanticTokensRegistrationOptions& o);
void from_json(const nlohmann::json& j, SemanticTokensRegistrationOptions& o);

struct MonikerOptions {};

void to_json(nlohmann::json& j, const MonikerOptions& o);
void from_json(const nlohmann::json& j, MonikerOptions& o);

struct MonikerRegistrationOptions {};

void to_json(nlohmann::json& j, const MonikerRegistrationOptions& o);
void from_json(const nlohmann::json& j, MonikerRegistrationOptions& o);

struct TypeHierarchyOptions {};

void to_json(nlohmann::json& j, const TypeHierarchyOptions& o);
void from_json(const nlohmann::json& j, TypeHierarchyOptions& o);

struct TypeHierarchyRegistrationOptions {};

void to_json(nlohmann::json& j, const TypeHierarchyRegistrationOptions& o);
void from_json(const nlohmann::json& j, TypeHierarchyRegistrationOptions& o);

struct InlineValueOptions {};

void to_json(nlohmann::json& j, const InlineValueOptions& o);
void from_json(const nlohmann::json& j, InlineValueOptions& o);

struct InlineValueRegistrationOptions {};

void to_json(nlohmann::json& j, const InlineValueRegistrationOptions& o);
void from_json(const nlohmann::json& j, InlineValueRegistrationOptions& o);

struct InlayHintOptions {};

void to_json(nlohmann::json& j, const InlayHintOptions& o);
void from_json(const nlohmann::json& j, InlayHintOptions& o);

struct InlayHintRegistrationOptions {};

void to_json(nlohmann::json& j, const InlayHintRegistrationOptions& o);
void from_json(const nlohmann::json& j, InlayHintRegistrationOptions& o);

struct DiagnosticOptions {};

void to_json(nlohmann::json& j, const DiagnosticOptions& o);
void from_json(const nlohmann::json& j, DiagnosticOptions& o);

struct DiagnosticRegistrationOptions {};

void to_json(nlohmann::json& j, const DiagnosticRegistrationOptions& o);
void from_json(const nlohmann::json& j, DiagnosticRegistrationOptions& o);

struct WorkspaceSymbolOptions {};

void to_json(nlohmann::json& j, const WorkspaceSymbolOptions& o);
void from_json(const nlohmann::json& j, WorkspaceSymbolOptions& o);

struct WorkspaceFoldersServerCapabilities {};

void to_json(nlohmann::json& j, const WorkspaceFoldersServerCapabilities& o);
void from_json(const nlohmann::json& j, WorkspaceFoldersServerCapabilities& o);

struct FileOperationRegistrationOptions {};

void to_json(nlohmann::json& j, const FileOperationRegistrationOptions& o);
void from_json(const nlohmann::json& j, FileOperationRegistrationOptions& o);

struct ServerCapabilities {
  std::optional<PositionEncodingKind> positionEncoding =
      PositionEncodingKind::UTF16;

  using TextDocumentSync =
      std::variant<TextDocumentSyncOptions, TextDocumentSyncKind>;
  std::optional<TextDocumentSync> textDocumentSync = std::nullopt;

  using NotebookDocumentSync = std::variant<
      NotebookDocumentSyncOptions, NotebookDocumentSyncRegistrationOptions>;
  std::optional<NotebookDocumentSync> notebookDocumentSync = std::nullopt;

  std::optional<CompletionOptions> completionProvider = std::nullopt;

  using HoverProvider = std::variant<bool, HoverOptions>;
  std::optional<HoverProvider> hoverProvider = std::nullopt;

  std::optional<SignatureHelpOptions> signatureHelpProvider = std::nullopt;

  using DeclarationProvider =
      std::variant<bool, DeclarationOptions, DeclarationRegistrationOptions>;
  std::optional<DeclarationProvider> declarationProvider = std::nullopt;

  using DefinitionProvider = std::variant<bool, DefinitionOptions>;
  std::optional<DefinitionProvider> definitionProvider = std::nullopt;

  using TypeDefinitionProvider = std::variant<
      bool, TypeDefinitionOptions, TypeDefinitionRegistrationOptions>;
  std::optional<TypeDefinitionProvider> typeDefinitionProvider = std::nullopt;

  using ImplementationProvider = std::variant<
      bool, ImplementationOptions, ImplementationRegistrationOptions>;
  std::optional<ImplementationProvider> implementationProvider = std::nullopt;

  using ReferencesProvider = std::variant<bool, ReferenceOptions>;
  std::optional<ReferencesProvider> referencesProvider = std::nullopt;

  using DocumentHighlightProvider =
      std::variant<bool, DocumentHighlightOptions>;
  std::optional<DocumentHighlightProvider> documentHighlightProvider =
      std::nullopt;

  using DocumentSymbolProvider = std::variant<bool, DocumentSymbolOptions>;
  std::optional<DocumentSymbolProvider> documentSymbolProvider = std::nullopt;

  using CodeActionProvider = std::variant<bool, CodeActionOptions>;
  std::optional<CodeActionProvider> codeActionProvider = std::nullopt;

  std::optional<CodeLensOptions> codeLensProvider = std::nullopt;

  std::optional<DocumentLinkOptions> documentLinkProvider = std::nullopt;

  using ColorProvider = std::variant<
      bool, DocumentColorOptions, DocumentColorRegistrationOptions>;
  std::optional<ColorProvider> colorProvider = std::nullopt;

  using DocumentFormattingProvider =
      std::variant<bool, DocumentFormattingOptions>;
  std::optional<DocumentFormattingProvider> documentFormattingProvider =
      std::nullopt;

  using DocumentRangeFormattingProvider =
      std::variant<bool, DocumentRangeFormattingOptions>;
  std::optional<DocumentRangeFormattingProvider>
      documentRangeFormattingProvider = std::nullopt;

  std::optional<DocumentOnTypeFormattingOptions>
      documentOnTypeFormattingProvider = std::nullopt;

  using RenameProvider = std::variant<bool, RenameOptions>;
  std::optional<RenameProvider> renameProvider = std::nullopt;

  using FoldingRangeProvider =
      std::variant<bool, FoldingRangeOptions, FoldingRangeRegistrationOptions>;
  std::optional<FoldingRangeProvider> foldingRangeProvider = std::nullopt;

  std::optional<ExecuteCommandOptions> executeCommandProvider = std::nullopt;

  using SelectionRangeProvider = std::variant<
      bool, SelectionRangeOptions, SelectionRangeRegistrationOptions>;
  std::optional<SelectionRangeProvider> selectionRangeProvider = std::nullopt;

  using LinkedEditingRangeProvider = std::variant<
      bool, LinkedEditingRangeOptions, LinkedEditingRangeRegistrationOptions>;
  std::optional<LinkedEditingRangeProvider> linkedEditingRangeProvider =
      std::nullopt;

  using CallHierarchyProvider = std::variant<
      bool, CallHierarchyOptions, CallHierarchyRegistrationOptions>;
  std::optional<CallHierarchyProvider> callHierarchyProvider = std::nullopt;

  using SemanticTokensProvider = std::variant<
      bool, SemanticTokensOptions, SemanticTokensRegistrationOptions>;
  std::optional<SemanticTokensProvider> semanticTokensProvider = std::nullopt;

  using MonikerProvider =
      std::variant<bool, MonikerOptions, MonikerRegistrationOptions>;
  std::optional<MonikerProvider> monikerProvider = std::nullopt;

  using TypeHierarchyProvider = std::variant<
      bool, TypeHierarchyOptions, TypeHierarchyRegistrationOptions>;
  std::optional<TypeHierarchyProvider> typeHierarchyProvider = std::nullopt;

  using InlineValueProvider =
      std::variant<bool, InlineValueOptions, InlineValueRegistrationOptions>;
  std::optional<InlineValueProvider> inlineValueProvider = std::nullopt;

  using InlayHintProvider =
      std::variant<bool, InlayHintOptions, InlayHintRegistrationOptions>;
  std::optional<InlayHintProvider> inlayHintProvider = std::nullopt;

  using DiagnosticProvider =
      std::variant<bool, DiagnosticOptions, DiagnosticRegistrationOptions>;
  std::optional<DiagnosticProvider> diagnosticProvider = std::nullopt;

  using WorkspaceSymbolProvider = std::variant<bool, WorkspaceSymbolOptions>;
  std::optional<WorkspaceSymbolProvider> workspaceSymbolProvider = std::nullopt;

  struct FileOperations {
    std::optional<FileOperationRegistrationOptions> didCreate;
    std::optional<FileOperationRegistrationOptions> willCreate;
    std::optional<FileOperationRegistrationOptions> didRename;
    std::optional<FileOperationRegistrationOptions> willRename;
    std::optional<FileOperationRegistrationOptions> didDelete;
    std::optional<FileOperationRegistrationOptions> willDelete;
  };

  struct Workspace {
    std::optional<WorkspaceFoldersServerCapabilities> workspaceFolders;

    std::optional<FileOperations> fileOperations;
  };

  std::optional<Workspace> workspace = std::nullopt;
  std::optional<nlohmann::json> experimental = std::nullopt;
};

void to_json(nlohmann::json& j, const ServerCapabilities::TextDocumentSync& o);
void from_json(
    const nlohmann::json& j, ServerCapabilities::TextDocumentSync& o);

void to_json(
    nlohmann::json& j, const ServerCapabilities::NotebookDocumentSync& o);
void from_json(
    const nlohmann::json& j, ServerCapabilities::NotebookDocumentSync& o);

void to_json(nlohmann::json& j, const ServerCapabilities::HoverProvider& o);
void from_json(const nlohmann::json& j, ServerCapabilities::HoverProvider& o);

void to_json(
    nlohmann::json& j, const ServerCapabilities::DeclarationProvider& o);
void from_json(
    const nlohmann::json& j, ServerCapabilities::DeclarationProvider& o);

void to_json(
    nlohmann::json& j, const ServerCapabilities::DefinitionProvider& o);
void from_json(
    const nlohmann::json& j, ServerCapabilities::DefinitionProvider& o);

void to_json(
    nlohmann::json& j, const ServerCapabilities::TypeDefinitionProvider& o);
void from_json(
    const nlohmann::json& j, ServerCapabilities::TypeDefinitionProvider& o);

void to_json(
    nlohmann::json& j, const ServerCapabilities::ImplementationProvider& o);
void from_json(
    const nlohmann::json& j, ServerCapabilities::ImplementationProvider& o);

void to_json(
    nlohmann::json& j, const ServerCapabilities::ReferencesProvider& o);
void from_json(
    const nlohmann::json& j, ServerCapabilities::ReferencesProvider& o);

void to_json(
    nlohmann::json& j, const ServerCapabilities::DocumentHighlightProvider& o);
void from_json(
    const nlohmann::json& j, ServerCapabilities::DocumentHighlightProvider& o);

void to_json(
    nlohmann::json& j, const ServerCapabilities::DocumentSymbolProvider& o);
void from_json(
    const nlohmann::json& j, ServerCapabilities::DocumentSymbolProvider& o);

void to_json(
    nlohmann::json& j, const ServerCapabilities::CodeActionProvider& o);
void from_json(
    const nlohmann::json& j, ServerCapabilities::CodeActionProvider& o);

void to_json(nlohmann::json& j, const ServerCapabilities::ColorProvider& o);
void from_json(const nlohmann::json& j, ServerCapabilities::ColorProvider& o);

void to_json(
    nlohmann::json& j, const ServerCapabilities::DocumentFormattingProvider& o);
void from_json(
    const nlohmann::json& j, ServerCapabilities::DocumentFormattingProvider& o);

void to_json(
    nlohmann::json& j,
    const ServerCapabilities::DocumentRangeFormattingProvider& o);
void from_json(
    const nlohmann::json& j,
    ServerCapabilities::DocumentRangeFormattingProvider& o);

void to_json(nlohmann::json& j, const ServerCapabilities::RenameProvider& o);
void from_json(const nlohmann::json& j, ServerCapabilities::RenameProvider& o);

void to_json(
    nlohmann::json& j, const ServerCapabilities::FoldingRangeProvider& o);
void from_json(
    const nlohmann::json& j, ServerCapabilities::FoldingRangeProvider& o);

void to_json(
    nlohmann::json& j, const ServerCapabilities::SelectionRangeProvider& o);
void from_json(
    const nlohmann::json& j, ServerCapabilities::SelectionRangeProvider& o);

void to_json(
    nlohmann::json& j, const ServerCapabilities::LinkedEditingRangeProvider& o);
void from_json(
    const nlohmann::json& j, ServerCapabilities::LinkedEditingRangeProvider& o);

void to_json(
    nlohmann::json& j, const ServerCapabilities::CallHierarchyProvider& o);
void from_json(
    const nlohmann::json& j, ServerCapabilities::CallHierarchyProvider& o);

void to_json(
    nlohmann::json& j, const ServerCapabilities::SemanticTokensProvider& o);
void from_json(
    const nlohmann::json& j, ServerCapabilities::SemanticTokensProvider& o);

void to_json(nlohmann::json& j, const ServerCapabilities::MonikerProvider& o);
void from_json(const nlohmann::json& j, ServerCapabilities::MonikerProvider& o);

void to_json(
    nlohmann::json& j, const ServerCapabilities::TypeHierarchyProvider& o);
void from_json(
    const nlohmann::json& j, ServerCapabilities::TypeHierarchyProvider& o);

void to_json(
    nlohmann::json& j, const ServerCapabilities::InlineValueProvider& o);
void from_json(
    const nlohmann::json& j, ServerCapabilities::InlineValueProvider& o);

void to_json(nlohmann::json& j, const ServerCapabilities::InlayHintProvider& o);
void from_json(
    const nlohmann::json& j, ServerCapabilities::InlayHintProvider& o);

void to_json(
    nlohmann::json& j, const ServerCapabilities::DiagnosticProvider& o);
void from_json(
    const nlohmann::json& j, ServerCapabilities::DiagnosticProvider& o);

void to_json(
    nlohmann::json& j, const ServerCapabilities::WorkspaceSymbolProvider& o);
void from_json(
    const nlohmann::json& j, ServerCapabilities::WorkspaceSymbolProvider& o);

void to_json(nlohmann::json& j, const ServerCapabilities::FileOperations& o);
void from_json(const nlohmann::json& j, ServerCapabilities::FileOperations& o);

void to_json(nlohmann::json& j, const ServerCapabilities::Workspace& o);
void from_json(const nlohmann::json& j, ServerCapabilities::Workspace& o);

void to_json(nlohmann::json& j, const ServerCapabilities& o);
void from_json(const nlohmann::json& j, ServerCapabilities& o);

}  // namespace lsp

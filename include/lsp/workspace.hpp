#pragma once

#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "lsp/basic.hpp"

namespace lsp {

// Workspace Symbols Request
struct WorkspaceSymbolParams : WorkDoneProgressParams, PartialResultParams {
  std::string query;
};

void to_json(nlohmann::json& j, const WorkspaceSymbolParams& p);
void from_json(const nlohmann::json& j, WorkspaceSymbolParams& p);

struct WorkspaceSymbol {
  std::string name;
  SymbolKind kind;
  std::optional<std::vector<SymbolTag>> tags;
  std::optional<std::string> containerName;
  using LocationOrUri = std::variant<Location, DocumentUri>;
  LocationOrUri location;
  std::optional<nlohmann::json> data;
};

void to_json(nlohmann::json& j, const WorkspaceSymbol& p);
void from_json(const nlohmann::json& j, WorkspaceSymbol& p);

// Keep simple, no variant
using WorkspaceSymbolResult = std::optional<std::vector<WorkspaceSymbol>>;

void to_json(nlohmann::json& j, const WorkspaceSymbolResult& p);
void from_json(const nlohmann::json& j, WorkspaceSymbolResult& p);

// Configuration Request
struct ConfigurationItem {
  std::optional<DocumentUri> scopeUri;
  std::optional<std::string> section;
};

void to_json(nlohmann::json& j, const ConfigurationItem& p);
void from_json(const nlohmann::json& j, ConfigurationItem& p);

struct ConfigurationParams {
  std::vector<ConfigurationItem> items;
};

void to_json(nlohmann::json& j, const ConfigurationParams& p);
void from_json(const nlohmann::json& j, ConfigurationParams& p);

using ConfigurationResult = std::vector<nlohmann::json>;

void to_json(nlohmann::json& j, const ConfigurationResult& p);
void from_json(const nlohmann::json& j, ConfigurationResult& p);

// DidChangeConfiguration Notification
struct DidChangeConfigurationParams {
  nlohmann::json settings;
};

void to_json(nlohmann::json& j, const DidChangeConfigurationParams& p);
void from_json(const nlohmann::json& j, DidChangeConfigurationParams& p);

// Workspace folders request
struct WorkspaceFolderParams {};

void to_json(nlohmann::json& j, const WorkspaceFolderParams& p);
void from_json(const nlohmann::json& j, WorkspaceFolderParams& p);

using WorkspaceFolderResult = std::optional<std::vector<WorkspaceFolder>>;

void to_json(nlohmann::json& j, const WorkspaceFolderResult& p);
void from_json(const nlohmann::json& j, WorkspaceFolderResult& p);

// DidChangeWorkspaceFolders Notification
struct WorkspaceFoldersChangeEvent {
  std::vector<WorkspaceFolder> added;
  std::vector<WorkspaceFolder> removed;
};

void to_json(nlohmann::json& j, const WorkspaceFoldersChangeEvent& p);
void from_json(const nlohmann::json& j, WorkspaceFoldersChangeEvent& p);

struct DidChangeWorkspaceFoldersParams {
  WorkspaceFoldersChangeEvent event;
};

void to_json(nlohmann::json& j, const DidChangeWorkspaceFoldersParams& p);
void from_json(const nlohmann::json& j, DidChangeWorkspaceFoldersParams& p);

// WillCreateFiles Request
struct FileCreate {
  std::string uri;
};

void to_json(nlohmann::json& j, const FileCreate& p);
void from_json(const nlohmann::json& j, FileCreate& p);

struct CreateFilesParams {
  std::vector<FileCreate> files;
};

void to_json(nlohmann::json& j, const CreateFilesParams& p);
void from_json(const nlohmann::json& j, CreateFilesParams& p);

void to_json(nlohmann::json& j, const std::optional<WorkspaceEdit>& p);
void from_json(const nlohmann::json& j, std::optional<WorkspaceEdit>& p);

// WillRenameFiles Request
struct FileRename {
  std::string oldUri;
  std::string newUri;
};

void to_json(nlohmann::json& j, const FileRename& p);
void from_json(const nlohmann::json& j, FileRename& p);

struct RenameFilesParams {
  std::vector<FileRename> files;
};

void to_json(nlohmann::json& j, const RenameFilesParams& p);
void from_json(const nlohmann::json& j, RenameFilesParams& p);

// WillDeleteFiles Request
struct FileDelete {
  std::string uri;
};

void to_json(nlohmann::json& j, const FileDelete& p);
void from_json(const nlohmann::json& j, FileDelete& p);

struct DeleteFilesParams {
  std::vector<FileDelete> files;
};

void to_json(nlohmann::json& j, const DeleteFilesParams& p);
void from_json(const nlohmann::json& j, DeleteFilesParams& p);

// DidChangeWatchedFiles Notification
enum class FileChangeType { Created = 1, Changed = 2, Deleted = 3 };

void to_json(nlohmann::json& j, const FileChangeType& p);
void from_json(const nlohmann::json& j, FileChangeType& p);

struct FileEvent {
  DocumentUri uri;
  FileChangeType type;
};

void to_json(nlohmann::json& j, const FileEvent& p);
void from_json(const nlohmann::json& j, FileEvent& p);

struct DidChangeWatchedFilesParams {
  std::vector<FileEvent> changes;
};

void to_json(nlohmann::json& j, const DidChangeWatchedFilesParams& p);
void from_json(const nlohmann::json& j, DidChangeWatchedFilesParams& p);

// Execute a command
struct ExecuteCommandParams : WorkDoneProgressParams {
  std::string command;
  std::optional<std::vector<nlohmann::json>> arguments;
};

void to_json(nlohmann::json& j, const ExecuteCommandParams& p);
void from_json(const nlohmann::json& j, ExecuteCommandParams& p);

struct ApplyWorkspaceEditParams {
  std::optional<std::string> label;
  WorkspaceEdit edit;
};

void to_json(nlohmann::json& j, const ApplyWorkspaceEditParams& p);
void from_json(const nlohmann::json& j, ApplyWorkspaceEditParams& p);

struct ApplyWorkspaceEditResult {
  bool applied;
  std::optional<std::string> failureReason;
  std::optional<int> failedChange;
};

void to_json(nlohmann::json& j, const ApplyWorkspaceEditResult& p);
void from_json(const nlohmann::json& j, ApplyWorkspaceEditResult& p);

}  // namespace lsp

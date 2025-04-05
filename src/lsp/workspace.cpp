#include "lsp/workspace.hpp"

#include "lsp/json_utils.hpp"

namespace lsp {

// Workspace Symbols Request
void to_json(nlohmann::json& j, const WorkspaceSymbolParams& p) {
  to_json_required(j, "query", p.query);
}
void from_json(const nlohmann::json& j, WorkspaceSymbolParams& p) {
  from_json_required(j, "query", p.query);
}

void to_json(nlohmann::json& j, const WorkspaceSymbol::LocationOrUri& p) {
  std::visit([&j](auto&& arg) { to_json(j, arg); }, p);
}

void from_json(const nlohmann::json& j, WorkspaceSymbol::LocationOrUri& p) {
  if (j.is_string()) {
    p = DocumentUri(j.get<std::string>());
  } else {
    p = Location(j);
  }
}

void to_json(nlohmann::json& j, const WorkspaceSymbol& p) {
  to_json_required(j, "name", p.name);
  to_json_required(j, "kind", p.kind);
  to_json_optional(j, "tags", p.tags);
  to_json_optional(j, "containerName", p.containerName);
  to_json_required(j, "location", p.location);
  to_json_optional(j, "data", p.data);
}

void from_json(const nlohmann::json& j, WorkspaceSymbol& p) {
  from_json_required(j, "name", p.name);
  from_json_required(j, "kind", p.kind);
  from_json_optional(j, "tags", p.tags);
  from_json_optional(j, "containerName", p.containerName);
  from_json_required(j, "location", p.location);
  from_json_optional(j, "data", p.data);
}

void to_json(nlohmann::json& j, const WorkspaceSymbolResult& p) {
  if (p.has_value()) {
    j = p.value();
  } else {
    j = nullptr;
  }
}
void from_json(const nlohmann::json& j, WorkspaceSymbolResult& p) {
  if (j.is_null()) {
    p = std::nullopt;
  } else {
    p = j.get<std::vector<WorkspaceSymbol>>();
  }
}

// Configuration Request
void to_json(nlohmann::json& j, const ConfigurationItem& p) {
  to_json_optional(j, "scopeUri", p.scopeUri);
  to_json_optional(j, "section", p.section);
}

void from_json(const nlohmann::json& j, ConfigurationItem& p) {
  from_json_optional(j, "scopeUri", p.scopeUri);
  from_json_optional(j, "section", p.section);
}

void to_json(nlohmann::json& j, const ConfigurationParams& p) {
  to_json_required(j, "items", p.items);
}

void from_json(const nlohmann::json& j, ConfigurationParams& p) {
  from_json_required(j, "items", p.items);
}

void to_json(nlohmann::json& j, const ConfigurationResult& p) { j = p; }

void from_json(const nlohmann::json& j, ConfigurationResult& p) {
  p = j.get<std::vector<nlohmann::json>>();
}

// DidChangeConfiguration Notification
void to_json(nlohmann::json& j, const DidChangeConfigurationParams& p) {
  to_json_required(j, "settings", p.settings);
}

void from_json(const nlohmann::json& j, DidChangeConfigurationParams& p) {
  from_json_required(j, "settings", p.settings);
}

// Workspace folders request
void to_json(nlohmann::json&, const WorkspaceFolderParams&) {};
void from_json(const nlohmann::json&, WorkspaceFolderParams&) {};

void to_json(nlohmann::json& j, const WorkspaceFolderResult& p) {
  if (p.has_value()) {
    j = p.value();
  } else {
    j = nullptr;
  }
}

void from_json(const nlohmann::json& j, WorkspaceFolderResult& p) {
  if (j.is_null()) {
    p = std::nullopt;
  } else {
    p = j.get<std::vector<WorkspaceFolder>>();
  }
}

// DidChangeWorkspaceFolders Notification
void to_json(nlohmann::json& j, const WorkspaceFoldersChangeEvent& p) {
  to_json_required(j, "added", p.added);
  to_json_required(j, "removed", p.removed);
}

void from_json(const nlohmann::json& j, WorkspaceFoldersChangeEvent& p) {
  from_json_required(j, "added", p.added);
  from_json_required(j, "removed", p.removed);
}

void to_json(nlohmann::json& j, const DidChangeWorkspaceFoldersParams& p) {
  to_json_required(j, "event", p.event);
}

void from_json(const nlohmann::json& j, DidChangeWorkspaceFoldersParams& p) {
  from_json_required(j, "event", p.event);
}

// WillCreateFiles Request
void to_json(nlohmann::json& j, const FileCreate& p) {
  to_json_required(j, "uri", p.uri);
}

void from_json(const nlohmann::json& j, FileCreate& p) {
  from_json_required(j, "uri", p.uri);
}

void to_json(nlohmann::json& j, const CreateFilesParams& p) {
  to_json_required(j, "files", p.files);
}

void from_json(const nlohmann::json& j, CreateFilesParams& p) {
  from_json_required(j, "files", p.files);
}

void to_json(nlohmann::json& j, const std::optional<WorkspaceEdit>& p) {
  if (p.has_value()) {
    j = p.value();
  } else {
    j = nullptr;
  }
}

void from_json(const nlohmann::json& j, std::optional<WorkspaceEdit>& p) {
  if (j.is_null()) {
    p = std::nullopt;
  } else {
    p = j.get<WorkspaceEdit>();
  }
}

// WillRenameFiles Request
void to_json(nlohmann::json& j, const FileRename& p) {
  to_json_required(j, "oldUri", p.oldUri);
  to_json_required(j, "newUri", p.newUri);
}

void from_json(const nlohmann::json& j, FileRename& p) {
  from_json_required(j, "oldUri", p.oldUri);
  from_json_required(j, "newUri", p.newUri);
}

void to_json(nlohmann::json& j, const RenameFilesParams& p) {
  to_json_required(j, "files", p.files);
}

void from_json(const nlohmann::json& j, RenameFilesParams& p) {
  from_json_required(j, "files", p.files);
}

void to_json(nlohmann::json& j, const FileDelete& p) {
  to_json_required(j, "uri", p.uri);
}

void from_json(const nlohmann::json& j, FileDelete& p) {
  from_json_required(j, "uri", p.uri);
}

void to_json(nlohmann::json& j, const DeleteFilesParams& p) {
  to_json_required(j, "files", p.files);
}

void from_json(const nlohmann::json& j, DeleteFilesParams& p) {
  from_json_required(j, "files", p.files);
}

// DidChangeWatchedFiles Notification
void to_json(nlohmann::json& j, const FileChangeType& p) {
  j = static_cast<int>(p);
}

void from_json(const nlohmann::json& j, FileChangeType& p) {
  p = static_cast<FileChangeType>(j.get<int>());
}

void to_json(nlohmann::json& j, const FileEvent& p) {
  to_json_required(j, "uri", p.uri);
  to_json_required(j, "type", p.type);
}

void from_json(const nlohmann::json& j, FileEvent& p) {
  from_json_required(j, "uri", p.uri);
  from_json_required(j, "type", p.type);
}

void to_json(nlohmann::json& j, const DidChangeWatchedFilesParams& p) {
  to_json_required(j, "changes", p.changes);
}

void from_json(const nlohmann::json& j, DidChangeWatchedFilesParams& p) {
  from_json_required(j, "changes", p.changes);
}

// Execute a command
void to_json(nlohmann::json& j, const ExecuteCommandParams& p) {
  to_json_required(j, "command", p.command);
  to_json_optional(j, "arguments", p.arguments);
}

void from_json(const nlohmann::json& j, ExecuteCommandParams& p) {
  from_json_required(j, "command", p.command);
  from_json_optional(j, "arguments", p.arguments);
}

void to_json(nlohmann::json& j, const ApplyWorkspaceEditParams& p) {
  to_json_optional(j, "label", p.label);
  to_json_required(j, "edit", p.edit);
}

void from_json(const nlohmann::json& j, ApplyWorkspaceEditParams& p) {
  from_json_optional(j, "label", p.label);
  from_json_required(j, "edit", p.edit);
}

void to_json(nlohmann::json& j, const ApplyWorkspaceEditResult& p) {
  to_json_required(j, "applied", p.applied);
  to_json_optional(j, "failureReason", p.failureReason);
  to_json_optional(j, "failedChange", p.failedChange);
}

void from_json(const nlohmann::json& j, ApplyWorkspaceEditResult& p) {
  from_json_required(j, "applied", p.applied);
  from_json_optional(j, "failureReason", p.failureReason);
  from_json_optional(j, "failedChange", p.failedChange);
}

}  // namespace lsp

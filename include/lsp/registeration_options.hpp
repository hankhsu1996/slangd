#pragma once

#include <optional>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "lsp/basic.hpp"

namespace lsp {

// DidChangeWatchedFiles Notification
using Pattern = std::string;

struct RelativePattern {
  WorkspaceFolder baseUri;
  Pattern pattern;
};

void to_json(nlohmann::json& j, const RelativePattern& p);
void from_json(const nlohmann::json& j, RelativePattern& p);

using GlobPattern = std::variant<Pattern, RelativePattern>;

void to_json(nlohmann::json& j, const GlobPattern& p);
void from_json(const nlohmann::json& j, GlobPattern& p);

enum class WatchKind {
  Create = 1,
  Change = 2,
  Delete = 4,
};

void to_json(nlohmann::json& j, const WatchKind& k);
void from_json(const nlohmann::json& j, WatchKind& k);

struct FileSystemWatcher {
  GlobPattern globPattern;
  std::optional<WatchKind> kind;
};

void to_json(nlohmann::json& j, const FileSystemWatcher& w);
void from_json(const nlohmann::json& j, FileSystemWatcher& w);

struct DidChangeWatchedFilesRegistrationOptions {
  std::vector<FileSystemWatcher> watchers;
};

void to_json(
    nlohmann::json& j, const DidChangeWatchedFilesRegistrationOptions& o);
void from_json(
    const nlohmann::json& j, DidChangeWatchedFilesRegistrationOptions& o);

}  // namespace lsp

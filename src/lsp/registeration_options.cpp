#include "lsp/registeration_options.hpp"

#include "lsp/json_utils.hpp"

namespace lsp {

// DidChangeWatchedFiles Notification
void to_json(nlohmann::json& j, const RelativePattern& p) {
  to_json_required(j, "baseUri", p.baseUri);
  to_json_required(j, "pattern", p.pattern);
}

void from_json(const nlohmann::json& j, RelativePattern& p) {
  from_json_required(j, "baseUri", p.baseUri);
  from_json_required(j, "pattern", p.pattern);
}

void to_json(nlohmann::json& j, const GlobPattern& p) {
  std::visit([&j](auto&& arg) { to_json(j, arg); }, p);
}

void from_json(const nlohmann::json& j, GlobPattern& p) {
  if (j.is_string()) {
    p = Pattern(j.get<std::string>());
  } else if (j.is_object()) {
    p = RelativePattern(j.get<RelativePattern>());
  } else {
    throw std::runtime_error("Invalid GlobPattern");
  }
}

void to_json(nlohmann::json& j, const WatchKind& k) { j = static_cast<int>(k); }

void from_json(const nlohmann::json& j, WatchKind& k) {
  k = static_cast<WatchKind>(j.get<int>());
}

void to_json(nlohmann::json& j, const FileSystemWatcher& w) {
  to_json_required(j, "globPattern", w.globPattern);
  to_json_optional(j, "kind", w.kind);
}

void from_json(const nlohmann::json& j, FileSystemWatcher& w) {
  from_json_required(j, "globPattern", w.globPattern);
  from_json_optional(j, "kind", w.kind);
}

void to_json(
    nlohmann::json& j, const DidChangeWatchedFilesRegistrationOptions& o) {
  to_json_required(j, "watchers", o.watchers);
}

void from_json(
    const nlohmann::json& j, DidChangeWatchedFilesRegistrationOptions& o) {
  from_json_required(j, "watchers", o.watchers);
}

}  // namespace lsp

#include "lsp/lifecycle.hpp"

#include <nlohmann/json.hpp>

#include "lsp/json_utils.hpp"

namespace lsp {

// Initialize Request
void to_json(nlohmann::json& j, const InitializeParams::ClientInfo& p) {
  j = nlohmann::json{{"name", p.name}};
  to_json_optional(j, "version", p.version);
}

void from_json(const nlohmann::json& j, InitializeParams::ClientInfo& p) {
  j.at("name").get_to(p.name);
  from_json_optional(j, "version", p.version);
}

void to_json(nlohmann::json& j, const InitializeParams& p) {
  to_json_optional(j, "processId", p.processId);
  to_json_optional(j, "clientInfo", p.clientInfo);
  to_json_optional(j, "locale", p.locale);
  to_json_optional(j, "rootPath", p.rootPath);
  to_json_optional(j, "rootUri", p.rootUri);
  to_json_optional(j, "initializationOptions", p.initializationOptions);
  to_json_optional(j, "capabilities", p.capabilities);
  to_json_optional(j, "trace", p.trace);
  to_json_optional(j, "workspaceFolders", p.workspaceFolders);
}

void from_json(const nlohmann::json& j, InitializeParams& p) {
  from_json_optional(j, "processId", p.processId);
  from_json_optional(j, "clientInfo", p.clientInfo);
  from_json_optional(j, "locale", p.locale);
  from_json_optional(j, "rootPath", p.rootPath);
  from_json_optional(j, "rootUri", p.rootUri);
  from_json_optional(j, "initializationOptions", p.initializationOptions);
  from_json_optional(j, "capabilities", p.capabilities);
  from_json_optional(j, "trace", p.trace);
  from_json_optional(j, "workspaceFolders", p.workspaceFolders);
}

void to_json(nlohmann::json& j, const InitializeResult::ServerInfo& p) {
  j = nlohmann::json{{"name", p.name}};
  to_json_optional(j, "version", p.version);
}

void from_json(const nlohmann::json& j, InitializeResult::ServerInfo& p) {
  j.at("name").get_to(p.name);
  from_json_optional(j, "version", p.version);
}

void to_json(nlohmann::json& j, const InitializeResult& p) {
  j = nlohmann::json{{"capabilities", p.capabilities}};
  to_json_optional(j, "serverInfo", p.serverInfo);
}

void from_json(const nlohmann::json& j, InitializeResult& p) {
  j.at("capabilities").get_to(p.capabilities);
  from_json_optional(j, "serverInfo", p.serverInfo);
}

// Initialized Notification
void to_json(nlohmann::json&, const InitializedParams&) {}
void from_json(const nlohmann::json&, InitializedParams&) {}

// Register Capability
void to_json(nlohmann::json& j, const Registration& p) {
  j = nlohmann::json{{"id", p.id}, {"method", p.method}};
  to_json_optional(j, "registerOptions", p.registerOptions);
}

void from_json(const nlohmann::json& j, Registration& p) {
  j.at("id").get_to(p.id);
  j.at("method").get_to(p.method);
  from_json_optional(j, "registerOptions", p.registerOptions);
}

void to_json(nlohmann::json& j, const RegistrationParams& p) {
  j = nlohmann::json{{"registrations", p.registrations}};
}

void from_json(const nlohmann::json& j, RegistrationParams& p) {
  j.at("registrations").get_to(p.registrations);
}

void to_json(nlohmann::json& j, const StaticRegistrationOptions& p) {
  to_json_optional(j, "id", p.id);
}

void from_json(const nlohmann::json& j, StaticRegistrationOptions& p) {
  from_json_optional(j, "id", p.id);
}

// Unregister Capability
void to_json(nlohmann::json& j, const Unregistration& p) {
  j = nlohmann::json{{"id", p.id}, {"method", p.method}};
}

void from_json(const nlohmann::json& j, Unregistration& p) {
  j.at("id").get_to(p.id);
  j.at("method").get_to(p.method);
}

void to_json(nlohmann::json& j, const UnregistrationParams& p) {
  j = nlohmann::json{{"unregistrations", p.unregistrations}};
}

void from_json(const nlohmann::json& j, UnregistrationParams& p) {
  j.at("unregistrations").get_to(p.unregistrations);
}

// SetTrace Notification
void to_json(nlohmann::json& j, const SetTraceParams& p) {
  j = nlohmann::json{{"value", p.value}};
}

void from_json(const nlohmann::json& j, SetTraceParams& p) {
  j.at("value").get_to(p.value);
}

// LogTrace Notification
void to_json(nlohmann::json& j, const LogTraceParams& p) {
  j = nlohmann::json{{"message", p.message}};
  to_json_optional(j, "verbose", p.verbose);
}

void from_json(const nlohmann::json& j, LogTraceParams& p) {
  j.at("message").get_to(p.message);
  from_json_optional(j, "verbose", p.verbose);
}

// Shutdown Request
void to_json(nlohmann::json&, const ShutdownParams&) {}
void from_json(const nlohmann::json&, ShutdownParams&) {}

void to_json(nlohmann::json&, const ShutdownResult&) {}
void from_json(const nlohmann::json&, ShutdownResult&) {}

// Exit Notification
void to_json(nlohmann::json&, const ExitParams&) {}
void from_json(const nlohmann::json&, ExitParams&) {}

}  // namespace lsp

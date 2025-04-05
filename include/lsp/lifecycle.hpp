#pragma once

#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "lsp/basic.hpp"
#include "lsp/client_capabilities.hpp"
#include "lsp/server_capabilities.hpp"

namespace lsp {

// Initialize Request
struct InitializeParams : WorkDoneProgressParams {
  std::optional<int> processId;
  struct ClientInfo {
    std::string name;
    std::optional<std::string> version;
  };
  std::optional<ClientInfo> clientInfo;
  std::optional<std::string> locale;
  std::optional<std::string> rootPath;
  std::optional<DocumentUri> rootUri;
  std::optional<nlohmann::json> initializationOptions;
  std::optional<ClientCapabilities> capabilities;
  std::optional<TraceValue> trace;
  std::optional<std::vector<WorkspaceFolder>> workspaceFolders;
};

void to_json(nlohmann::json& j, const InitializeParams::ClientInfo& p);
void from_json(const nlohmann::json& j, InitializeParams::ClientInfo& p);

void to_json(nlohmann::json& j, const InitializeParams& p);
void from_json(const nlohmann::json& j, InitializeParams& p);

struct InitializeResult {
  ServerCapabilities capabilities;
  struct ServerInfo {
    std::string name;
    std::optional<std::string> version;
  };
  std::optional<ServerInfo> serverInfo;
};

void to_json(nlohmann::json& j, const InitializeResult::ServerInfo& p);
void from_json(const nlohmann::json& j, InitializeResult::ServerInfo& p);

void to_json(nlohmann::json& j, const InitializeResult& p);
void from_json(const nlohmann::json& j, InitializeResult& p);

// Initialized Notification
struct InitializedParams {};

void to_json(nlohmann::json& j, const InitializedParams& p);
void from_json(const nlohmann::json& j, InitializedParams& p);

// Register Capability
struct Registration {
  std::string id;
  std::string method;
  std::optional<nlohmann::json> registerOptions;
};

void to_json(nlohmann::json& j, const Registration& p);
void from_json(const nlohmann::json& j, Registration& p);

struct RegistrationParams {
  std::vector<Registration> registrations;
};

void to_json(nlohmann::json& j, const RegistrationParams& p);
void from_json(const nlohmann::json& j, RegistrationParams& p);

struct RegistrationResult {};

void to_json(nlohmann::json&, const RegistrationResult&);
void from_json(const nlohmann::json&, RegistrationResult&);

struct StaticRegistrationOptions {
  std::optional<std::string> id;
};

void to_json(nlohmann::json& j, const StaticRegistrationOptions& p);
void from_json(const nlohmann::json& j, StaticRegistrationOptions& p);

// Unregister Capability
struct Unregistration {
  std::string id;
  std::string method;
};

void to_json(nlohmann::json& j, const Unregistration& p);
void from_json(const nlohmann::json& j, Unregistration& p);

struct UnregistrationParams {
  std::vector<Unregistration> unregistrations;
};

void to_json(nlohmann::json& j, const UnregistrationParams& p);
void from_json(const nlohmann::json& j, UnregistrationParams& p);

struct UnregistrationResult {};

void to_json(nlohmann::json&, const UnregistrationResult&);
void from_json(const nlohmann::json&, UnregistrationResult&);

// SetTrace Notification
struct SetTraceParams {
  TraceValue value;
};

void to_json(nlohmann::json& j, const SetTraceParams& p);
void from_json(const nlohmann::json& j, SetTraceParams& p);

// LogTrace Notification
struct LogTraceParams {
  std::string message;
  std::optional<std::string> verbose;
};

void to_json(nlohmann::json& j, const LogTraceParams& p);
void from_json(const nlohmann::json& j, LogTraceParams& p);

// Shutdown Request
struct ShutdownParams {};

void to_json(nlohmann::json& j, const ShutdownParams& p);
void from_json(const nlohmann::json& j, ShutdownParams& p);

struct ShutdownResult {};

void to_json(nlohmann::json& j, const ShutdownResult& p);
void from_json(const nlohmann::json& j, ShutdownResult& p);

// Exit Notification
struct ExitParams {};

void to_json(nlohmann::json& j, const ExitParams& p);
void from_json(const nlohmann::json& j, ExitParams& p);

}  // namespace lsp

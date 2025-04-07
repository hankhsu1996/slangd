#include "lsp/navigation.hpp"

#include <nlohmann/json.hpp>

#include "lsp/json_utils.hpp"

namespace lsp {

// Goto Declaration Request
void to_json(nlohmann::json& j, const DeclarationParams& p) {
  to_json_required(j, "textDocument", p.textDocument);
  to_json_required(j, "position", p.position);
  to_json_optional(j, "workDoneToken", p.workDoneToken);
  to_json_optional(j, "partialResultToken", p.partialResultToken);
}

void from_json(const nlohmann::json& j, DeclarationParams& p) {
  from_json_required(j, "textDocument", p.textDocument);
  from_json_required(j, "position", p.position);
  from_json_optional(j, "workDoneToken", p.workDoneToken);
  from_json_optional(j, "partialResultToken", p.partialResultToken);
}

void to_json(nlohmann::json& j, const DeclarationResult& r) {
  if (r) {
    if (std::holds_alternative<Location>(*r)) {
      to_json(j, std::get<Location>(*r));
    } else if (std::holds_alternative<std::vector<Location>>(*r)) {
      to_json(j, std::get<std::vector<Location>>(*r));
    }
  }
}

void from_json(const nlohmann::json& j, DeclarationResult& r) {
  if (j.is_array()) {
    r = std::vector<LocationLink>{};
  } else if (j.is_object()) {
    r = Location{};
  }
}

// Goto Definition Request
void to_json(nlohmann::json& j, const DefinitionParams& p) {
  to_json_required(j, "textDocument", p.textDocument);
  to_json_required(j, "position", p.position);
  to_json_optional(j, "workDoneToken", p.workDoneToken);
  to_json_optional(j, "partialResultToken", p.partialResultToken);
}

void from_json(const nlohmann::json& j, DefinitionParams& p) {
  from_json_required(j, "textDocument", p.textDocument);
  from_json_required(j, "position", p.position);
  from_json_optional(j, "workDoneToken", p.workDoneToken);
  from_json_optional(j, "partialResultToken", p.partialResultToken);
}

// Goto Type Definition Request
void to_json(nlohmann::json& j, const TypeDefinitionParams& p) {
  to_json_required(j, "textDocument", p.textDocument);
  to_json_required(j, "position", p.position);
  to_json_optional(j, "workDoneToken", p.workDoneToken);
  to_json_optional(j, "partialResultToken", p.partialResultToken);
}

void from_json(const nlohmann::json& j, TypeDefinitionParams& p) {
  from_json_required(j, "textDocument", p.textDocument);
  from_json_required(j, "position", p.position);
  from_json_optional(j, "workDoneToken", p.workDoneToken);
  from_json_optional(j, "partialResultToken", p.partialResultToken);
}

// Goto Implementation Request
void to_json(nlohmann::json& j, const ImplementationParams& p) {
  to_json_required(j, "textDocument", p.textDocument);
  to_json_required(j, "position", p.position);
  to_json_optional(j, "workDoneToken", p.workDoneToken);
  to_json_optional(j, "partialResultToken", p.partialResultToken);
}

void from_json(const nlohmann::json& j, ImplementationParams& p) {
  from_json_required(j, "textDocument", p.textDocument);
  from_json_required(j, "position", p.position);
  from_json_optional(j, "workDoneToken", p.workDoneToken);
  from_json_optional(j, "partialResultToken", p.partialResultToken);
}

void to_json(nlohmann::json& j, const ReferenceContext& c) {
  to_json_required(j, "includeDeclaration", c.includeDeclaration);
}

void from_json(const nlohmann::json& j, ReferenceContext& c) {
  from_json_required(j, "includeDeclaration", c.includeDeclaration);
}

void to_json(nlohmann::json& j, const ReferenceParams& p) {
  to_json_required(j, "textDocument", p.textDocument);
  to_json_required(j, "position", p.position);
  to_json_optional(j, "workDoneToken", p.workDoneToken);
  to_json_optional(j, "partialResultToken", p.partialResultToken);
  to_json_required(j, "context", p.context);
}

void from_json(const nlohmann::json& j, ReferenceParams& p) {
  from_json_required(j, "textDocument", p.textDocument);
  from_json_required(j, "position", p.position);
  from_json_optional(j, "workDoneToken", p.workDoneToken);
  from_json_optional(j, "partialResultToken", p.partialResultToken);
  from_json_required(j, "context", p.context);
}

}  // namespace lsp

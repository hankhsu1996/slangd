#pragma once

#include <optional>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "lsp/basic.hpp"

namespace lsp {

// Goto Declaration Request
struct DeclarationParams : TextDocumentPositionParams,
                           WorkDoneProgressParams,
                           PartialResultParams {};

void to_json(nlohmann::json& j, const DeclarationParams& p);
void from_json(const nlohmann::json& j, DeclarationParams& p);

using DeclarationResult = std::optional<
    std::variant<Location, std::vector<Location>, std::vector<LocationLink>>>;

void to_json(nlohmann::json& j, const DeclarationResult& r);
void from_json(const nlohmann::json& j, DeclarationResult& r);

// Goto Definition Request
struct DefinitionParams : TextDocumentPositionParams,
                          WorkDoneProgressParams,
                          PartialResultParams {};

void to_json(nlohmann::json& j, const DefinitionParams& p);
void from_json(const nlohmann::json& j, DefinitionParams& p);

using DefinitionResult = std::optional<
    std::variant<Location, std::vector<Location>, std::vector<LocationLink>>>;

// Goto Type Definition Request
struct TypeDefinitionParams : TextDocumentPositionParams,
                              WorkDoneProgressParams,
                              PartialResultParams {};

void to_json(nlohmann::json& j, const TypeDefinitionParams& p);
void from_json(const nlohmann::json& j, TypeDefinitionParams& p);

using TypeDefinitionResult = std::optional<
    std::variant<Location, std::vector<Location>, std::vector<LocationLink>>>;

// Goto Implementation Request
struct ImplementationParams : TextDocumentPositionParams,
                              WorkDoneProgressParams,
                              PartialResultParams {};

void to_json(nlohmann::json& j, const ImplementationParams& p);
void from_json(const nlohmann::json& j, ImplementationParams& p);

using ImplementationResult = std::optional<
    std::variant<Location, std::vector<Location>, std::vector<LocationLink>>>;

// Find References Request
struct ReferenceContext {
  bool includeDeclaration;
};

void to_json(nlohmann::json& j, const ReferenceContext& c);
void from_json(const nlohmann::json& j, ReferenceContext& c);

struct ReferenceParams : TextDocumentPositionParams,
                         WorkDoneProgressParams,
                         PartialResultParams {
  ReferenceContext context{};
};

void to_json(nlohmann::json& j, const ReferenceParams& p);
void from_json(const nlohmann::json& j, ReferenceParams& p);

using ReferenceResult = std::optional<std::vector<Location>>;

}  // namespace lsp

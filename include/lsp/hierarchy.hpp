#pragma once

#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "lsp/basic.hpp"

namespace lsp {

// Prepare Call Hierarchy Request
struct CallHierarchyPrepareParams : TextDocumentPositionParams,
                                    WorkDoneProgressParams {};

void to_json(nlohmann::json& j, const CallHierarchyPrepareParams& p);
void from_json(const nlohmann::json& j, CallHierarchyPrepareParams& p);

struct CallHierarchyItem {
  std::string name;
  SymbolKind kind;
  std::optional<std::vector<SymbolTag>> tags;
  std::optional<std::string> detail;
  DocumentUri uri;
  Range range;
  Range selectionRange;
  std::optional<nlohmann::json> data;
};

void to_json(nlohmann::json& j, const CallHierarchyItem& i);
void from_json(const nlohmann::json& j, CallHierarchyItem& i);

struct CallHierarchyPrepareResult {
  std::optional<std::vector<CallHierarchyItem>> items;
};

void to_json(nlohmann::json& j, const CallHierarchyPrepareResult& r);
void from_json(const nlohmann::json& j, CallHierarchyPrepareResult& r);

// Call Hierarchy Incoming Calls

struct CallHierarchyIncomingCallsParams : WorkDoneProgressParams,
                                          PartialResultParams {
  CallHierarchyItem item;
};

void to_json(nlohmann::json& j, const CallHierarchyIncomingCallsParams& p);
void from_json(const nlohmann::json& j, CallHierarchyIncomingCallsParams& p);

struct CallHierarchyIncomingCall {
  CallHierarchyItem from;
  std::vector<Range> fromRanges;
};

void to_json(nlohmann::json& j, const CallHierarchyIncomingCall& c);
void from_json(const nlohmann::json& j, CallHierarchyIncomingCall& c);

using CallHierarchyIncomingCallsResult =
    std::optional<std::vector<CallHierarchyIncomingCall>>;

void to_json(nlohmann::json& j, const CallHierarchyIncomingCallsResult& r);
void from_json(const nlohmann::json& j, CallHierarchyIncomingCallsResult& r);

// Call Hierarchy Outgoing Calls
struct CallHierarchyOutgoingCallsParams : WorkDoneProgressParams,
                                          PartialResultParams {
  CallHierarchyItem item;
};

void to_json(nlohmann::json& j, const CallHierarchyOutgoingCallsParams& p);
void from_json(const nlohmann::json& j, CallHierarchyOutgoingCallsParams& p);

struct CallHierarchyOutgoingCall {
  CallHierarchyItem to;
  std::vector<Range> fromRanges;
};

void to_json(nlohmann::json& j, const CallHierarchyOutgoingCall& c);
void from_json(const nlohmann::json& j, CallHierarchyOutgoingCall& c);

using CallHierarchyOutgoingCallsResult =
    std::optional<std::vector<CallHierarchyOutgoingCall>>;

void to_json(nlohmann::json& j, const CallHierarchyOutgoingCallsResult& r);
void from_json(const nlohmann::json& j, CallHierarchyOutgoingCallsResult& r);

// Prepare Type Hierarchy Request
struct TypeHierarchyPrepareParams : TextDocumentPositionParams,
                                    WorkDoneProgressParams {};

void to_json(nlohmann::json& j, const TypeHierarchyPrepareParams& p);
void from_json(const nlohmann::json& j, TypeHierarchyPrepareParams& p);

struct TypeHierarchyItem {
  std::string name;
  SymbolKind kind;
  std::optional<std::vector<SymbolTag>> tags;
  std::optional<std::string> detail;
  DocumentUri uri;
  Range range;
  Range selectionRange;
  std::optional<nlohmann::json> data;
};

void to_json(nlohmann::json& j, const TypeHierarchyItem& i);
void from_json(const nlohmann::json& j, TypeHierarchyItem& i);

using TypeHierarchyPrepareResult =
    std::optional<std::vector<TypeHierarchyItem>>;

void to_json(nlohmann::json& j, const TypeHierarchyPrepareResult& r);
void from_json(const nlohmann::json& j, TypeHierarchyPrepareResult& r);

// Type Hierarchy Supertypes
struct TypeHierarchySupertypesParams : WorkDoneProgressParams,
                                       PartialResultParams {
  TypeHierarchyItem item;
};

void to_json(nlohmann::json& j, const TypeHierarchySupertypesParams& p);
void from_json(const nlohmann::json& j, TypeHierarchySupertypesParams& p);

using TypeHierarchySupertypesResult =
    std::optional<std::vector<TypeHierarchyItem>>;

void to_json(nlohmann::json& j, const TypeHierarchySupertypesResult& r);
void from_json(const nlohmann::json& j, TypeHierarchySupertypesResult& r);

// Type Hierarchy Subtypes
struct TypeHierarchySubtypesParams : WorkDoneProgressParams,
                                     PartialResultParams {
  TypeHierarchyItem item;
};

void to_json(nlohmann::json& j, const TypeHierarchySubtypesParams& p);
void from_json(const nlohmann::json& j, TypeHierarchySubtypesParams& p);

using TypeHierarchySubtypesResult =
    std::optional<std::vector<TypeHierarchyItem>>;

void to_json(nlohmann::json& j, const TypeHierarchySubtypesResult& r);
void from_json(const nlohmann::json& j, TypeHierarchySubtypesResult& r);

}  // namespace lsp

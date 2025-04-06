#pragma once

#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "lsp/basic.hpp"

namespace lsp {

// Signature Help Request
enum class SignatureHelpTriggerKind {
  kInvoked,
  kTriggerCharacter,
  kContentChange,
};

struct ParameterInformation {
  std::string label;
  std::optional<std::string> documentation;
};

struct SignatureInformation {
  std::string label;
  std::optional<std::string> documentation;
  std::optional<std::vector<ParameterInformation>> parameters;
  std::optional<int> activeParameter;
};

struct SignatureHelp {
  std::vector<SignatureInformation> signatures;
  std::optional<int> activeSignature;
  std::optional<int> activeParameter;
};

struct SignatureHelpContext {
  SignatureHelpTriggerKind triggerKind;
  std::optional<std::string> triggerCharacter;
  bool isRetrigger;
  std::optional<SignatureHelp> activeSignatureHelp;
};

struct SignatureHelpParams : TextDocumentPositionParams,
                             WorkDoneProgressParams {
  std::optional<SignatureHelpContext> context;
};

}  // namespace lsp

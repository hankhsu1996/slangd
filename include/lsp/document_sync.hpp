#pragma once

#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "lsp/basic.hpp"

namespace lsp {

// DidOpenTextDocument Notification
struct DidOpenTextDocumentParams {
  TextDocumentItem textDocument;
};

void to_json(nlohmann::json& j, const DidOpenTextDocumentParams& p);
void from_json(const nlohmann::json& j, DidOpenTextDocumentParams& p);

// DidChangeTextDocument Notification
struct TextDocumentContentPartialChangeEvent {
  Range range;
  std::optional<uint32_t> rangeLength;
  std::string text;
};

void to_json(nlohmann::json& j, const TextDocumentContentPartialChangeEvent& e);
void from_json(
    const nlohmann::json& j, TextDocumentContentPartialChangeEvent& e);

struct TextDocumentContentFullChangeEvent {
  std::string text;
};

void to_json(nlohmann::json& j, const TextDocumentContentFullChangeEvent& e);
void from_json(const nlohmann::json& j, TextDocumentContentFullChangeEvent& e);

using TextDocumentContentChangeEvent = std::variant<
    TextDocumentContentPartialChangeEvent, TextDocumentContentFullChangeEvent>;

void to_json(nlohmann::json& j, const TextDocumentContentChangeEvent& e);
void from_json(const nlohmann::json& j, TextDocumentContentChangeEvent& e);

struct DidChangeTextDocumentParams {
  VersionedTextDocumentIdentifier textDocument;
  std::vector<TextDocumentContentChangeEvent> contentChanges;
};

void to_json(nlohmann::json& j, const DidChangeTextDocumentParams& p);
void from_json(const nlohmann::json& j, DidChangeTextDocumentParams& p);

// WillSaveTextDocument Notification
enum class TextDocumentSaveReason {
  kManual = 1,
  kAfterDelay = 2,
  kFocusOut = 3,
};

void to_json(nlohmann::json& j, const TextDocumentSaveReason& r);
void from_json(const nlohmann::json& j, TextDocumentSaveReason& r);

struct WillSaveTextDocumentParams {
  TextDocumentIdentifier textDocument;
  TextDocumentSaveReason reason{};
};

void to_json(nlohmann::json& j, const WillSaveTextDocumentParams& p);
void from_json(const nlohmann::json& j, WillSaveTextDocumentParams& p);

struct WillSaveTextDocumentResult {
  std::optional<std::vector<TextEdit>> textEdits;
};

void to_json(nlohmann::json& j, const WillSaveTextDocumentResult& p);
void from_json(const nlohmann::json& j, WillSaveTextDocumentResult& p);

// DidSaveTextDocument Notification
struct DidSaveTextDocumentParams {
  TextDocumentIdentifier textDocument;
  std::optional<std::string> text;
};

void to_json(nlohmann::json& j, const DidSaveTextDocumentParams& p);
void from_json(const nlohmann::json& j, DidSaveTextDocumentParams& p);

// DidCloseTextDocument Notification

struct DidCloseTextDocumentParams {
  TextDocumentIdentifier textDocument;
};

void to_json(nlohmann::json& j, const DidCloseTextDocumentParams& p);
void from_json(const nlohmann::json& j, DidCloseTextDocumentParams& p);

}  // namespace lsp

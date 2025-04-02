#include "lsp/document_sync.hpp"

#include "lsp/json_utils.hpp"

namespace lsp {

// DidOpenTextDocument Notification
void to_json(nlohmann::json& j, const DidOpenTextDocumentParams& p) {
  j = nlohmann::json{{"textDocument", p.textDocument}};
}

void from_json(const nlohmann::json& j, DidOpenTextDocumentParams& p) {
  j.at("textDocument").get_to(p.textDocument);
}

// DidChangeTextDocument Notification
void to_json(
    nlohmann::json& j, const TextDocumentContentPartialChangeEvent& e) {
  j = nlohmann::json{{"range", e.range}, {"text", e.text}};
  to_json_optional(j, "rangeLength", e.rangeLength);
}

void from_json(
    const nlohmann::json& j, TextDocumentContentPartialChangeEvent& e) {
  j.at("range").get_to(e.range);
  from_json_optional(j, "rangeLength", e.rangeLength);
  j.at("text").get_to(e.text);
}

void to_json(nlohmann::json& j, const TextDocumentContentFullChangeEvent& e) {
  j = nlohmann::json{{"text", e.text}};
}

void from_json(const nlohmann::json& j, TextDocumentContentFullChangeEvent& e) {
  j.at("text").get_to(e.text);
}

void to_json(nlohmann::json& j, const TextDocumentContentChangeEvent& e) {
  std::visit([&j](auto&& arg) { to_json(j, arg); }, e);
}

void from_json(const nlohmann::json& j, TextDocumentContentChangeEvent& e) {
  if (j.contains("range")) {
    e = j.get<TextDocumentContentPartialChangeEvent>();
  } else {
    e = j.get<TextDocumentContentFullChangeEvent>();
  }
}

void to_json(nlohmann::json& j, const DidChangeTextDocumentParams& p) {
  j = nlohmann::json{
      {"textDocument", p.textDocument}, {"contentChanges", p.contentChanges}};
}

void from_json(const nlohmann::json& j, DidChangeTextDocumentParams& p) {
  j.at("textDocument").get_to(p.textDocument);
  j.at("contentChanges").get_to(p.contentChanges);
}

// WillSaveTextDocument Notification
void to_json(nlohmann::json& j, const TextDocumentSaveReason& r) {
  j = nlohmann::json(static_cast<int>(r));
}

void from_json(const nlohmann::json& j, TextDocumentSaveReason& r) {
  r = static_cast<TextDocumentSaveReason>(j.get<int>());
}

void to_json(nlohmann::json& j, const WillSaveTextDocumentParams& p) {
  j = nlohmann::json{{"textDocument", p.textDocument}, {"reason", p.reason}};
}

void from_json(const nlohmann::json& j, WillSaveTextDocumentParams& p) {
  j.at("textDocument").get_to(p.textDocument);
  j.at("reason").get_to(p.reason);
}

void to_json(nlohmann::json& j, const WillSaveTextDocumentResult& p) {
  j = nlohmann::json{};
  to_json_optional(j, "textEdits", p.textEdits);
}

void from_json(const nlohmann::json& j, WillSaveTextDocumentResult& p) {
  from_json_optional(j, "textEdits", p.textEdits);
}

// DidSaveTextDocument Notification
void to_json(nlohmann::json& j, const DidSaveTextDocumentParams& p) {
  j = nlohmann::json{{"textDocument", p.textDocument}};
  to_json_optional(j, "text", p.text);
}

void from_json(const nlohmann::json& j, DidSaveTextDocumentParams& p) {
  j.at("textDocument").get_to(p.textDocument);
  from_json_optional(j, "text", p.text);
}

// DidCloseTextDocument Notification
void to_json(nlohmann::json& j, const DidCloseTextDocumentParams& p) {
  j = nlohmann::json{{"textDocument", p.textDocument}};
}

void from_json(const nlohmann::json& j, DidCloseTextDocumentParams& p) {
  j.at("textDocument").get_to(p.textDocument);
}
}  // namespace lsp

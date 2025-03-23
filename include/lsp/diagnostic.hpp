#pragma once

#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "lsp/basic.hpp"

namespace lsp {

/**
 * Diagnostic severities as defined by the LSP specification
 */
enum class DiagnosticSeverity {
  Error = 1,
  Warning = 2,
  Information = 3,
  Hint = 4
};

/**
 * Diagnostic tags as defined by the LSP specification
 */
enum class DiagnosticTag { Unnecessary = 1, Deprecated = 2 };

/**
 * Code description structure with href to documentation
 */
struct CodeDescription {
  std::string href;  // URI to open documentation
};

/**
 * Related diagnostic information
 */
struct DiagnosticRelatedInformation {
  struct {
    std::string uri;
    Range range;
  } location;
  std::string message;
};

/**
 * Diagnostic as defined by the LSP specification
 */
struct Diagnostic {
  Range range;                                     // Error location
  std::optional<DiagnosticSeverity> severity;      // Severity level
  std::optional<std::string> code;                 // Error code
  std::optional<CodeDescription> codeDescription;  // Code documentation link
  std::optional<std::string> source;               // Diagnostic source
  std::string message;                             // Error message
  std::optional<std::vector<DiagnosticTag>> tags;  // Diagnostic tags
  std::optional<std::vector<DiagnosticRelatedInformation>> relatedInformation;
  std::optional<nlohmann::json> data;  // Additional data
};

/**
 * Parameters for the textDocument/publishDiagnostics notification
 */
struct PublishDiagnosticsParams {
  std::string uri;                      // Document URI
  std::optional<int> version;           // Document version
  std::vector<Diagnostic> diagnostics;  // List of diagnostics
};

// JSON conversion functions
void to_json(nlohmann::json& j, const DiagnosticSeverity& s);
void from_json(const nlohmann::json& j, DiagnosticSeverity& s);

void to_json(nlohmann::json& j, const DiagnosticTag& t);
void from_json(const nlohmann::json& j, DiagnosticTag& t);

void to_json(nlohmann::json& j, const CodeDescription& c);
void from_json(const nlohmann::json& j, CodeDescription& c);

void to_json(nlohmann::json& j, const DiagnosticRelatedInformation& r);
void from_json(const nlohmann::json& j, DiagnosticRelatedInformation& r);

void to_json(nlohmann::json& j, const Diagnostic& d);
void from_json(const nlohmann::json& j, Diagnostic& d);

void to_json(nlohmann::json& j, const PublishDiagnosticsParams& p);
void from_json(const nlohmann::json& j, PublishDiagnosticsParams& p);

}  // namespace lsp

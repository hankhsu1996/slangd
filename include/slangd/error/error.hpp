#pragma once

#include <expected>
#include <string>
#include <unordered_map>
#include <utility>

namespace slangd {

/**
 * @brief Error codes for the Slangd LSP server
 */
enum class SlangdErrorCode {
  // No error
  Success = 0,

  // File system errors
  FileNotFound,
  FileAccessDenied,
  FileInvalidEncoding,

  // Parser errors
  SyntaxError,
  ParseFailed,

  // Compilation errors
  CompilationFailed,
  ElaborationFailed,

  // LSP errors
  InvalidRequest,
  UnsupportedRequest,

  // Internal errors
  SlangInternalError,
  UnknownError
};

/**
 * @brief Error class for the Slangd LSP server
 */
class SlangdError {
 public:
  // Default constructor - no error
  SlangdError() : code_(SlangdErrorCode::Success) {}

  // Construct from error code
  explicit SlangdError(SlangdErrorCode code) : code_(code) {
    message_ = GetDefaultMessage(code);
  }

  // Construct from error code and message
  SlangdError(SlangdErrorCode code, std::string message)
      : code_(code), message_(std::move(message)) {}

  // Check if there is no error
  bool ok() const { return code_ == SlangdErrorCode::Success; }

  // Get the error code
  SlangdErrorCode code() const { return code_; }

  // Get the error message (may be empty)
  const std::string& message() const { return message_; }

  // Allow if(error) checks
  explicit operator bool() const { return !ok(); }

  // Get the default message for an error code
  static std::string GetDefaultMessage(SlangdErrorCode code) {
    static const std::unordered_map<SlangdErrorCode, std::string> messages = {
        {SlangdErrorCode::Success, "Success"},
        {SlangdErrorCode::FileNotFound, "File not found"},
        {SlangdErrorCode::FileAccessDenied, "Access to file denied"},
        {SlangdErrorCode::FileInvalidEncoding, "Invalid file encoding"},
        {SlangdErrorCode::SyntaxError, "Syntax error"},
        {SlangdErrorCode::ParseFailed, "Failed to parse file"},
        {SlangdErrorCode::CompilationFailed, "Compilation failed"},
        {SlangdErrorCode::ElaborationFailed, "Elaboration failed"},
        {SlangdErrorCode::InvalidRequest, "Invalid request"},
        {SlangdErrorCode::UnsupportedRequest, "Unsupported request"},
        {SlangdErrorCode::SlangInternalError, "Internal slang error"},
        {SlangdErrorCode::UnknownError, "Unknown error"}};

    auto it = messages.find(code);
    if (it != messages.end()) {
      return it->second;
    }
    return "Unknown error";
  }

  // Factory method for creating an error
  static SlangdError Make(
      SlangdErrorCode code, const std::string& details = "") {
    if (details.empty()) {
      return SlangdError(code);
    }

    std::string message = GetDefaultMessage(code);

    // If we have details, append them to the message
    if (!details.empty()) {
      message += ": " + details;
    }

    return SlangdError(code, message);
  }

  // Factory method for creating an unexpected error
  template <typename T = void>
  static std::unexpected<SlangdError> Unexpected(
      SlangdErrorCode code, const std::string& details = "") {
    return std::unexpected<SlangdError>(Make(code, details));
  }

 private:
  SlangdErrorCode code_ = SlangdErrorCode::Success;
  std::string message_;
};

}  // namespace slangd

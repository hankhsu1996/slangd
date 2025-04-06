#pragma once

#include <expected>
#include <string>
#include <utility>

#include <jsonrpc/error/error.hpp>

namespace lsp::error {

using RpcError = jsonrpc::error::RpcError;
using RpcErrorCode = jsonrpc::error::RpcErrorCode;

enum class LspErrorCode {
  // RPC errors passthrough
  kParseError,
  kInvalidRequest,
  kMethodNotFound,
  kInvalidParams,
  kInternalError,
  kServerError,
  kTransportError,
  kTimeoutError,
  kClientError,

  // LSP errors
  kMethodNotImplemented,
  kDocumentNotOpen,
  kDocumentNotFound,

  // Unknown error
  kUnknownError,
};

namespace detail {

inline auto DefaultMessageFor(LspErrorCode code) -> std::string {
  switch (code) {
    case LspErrorCode::kParseError:
      return "Parse error";
    case LspErrorCode::kInvalidRequest:
      return "Invalid request";
    case LspErrorCode::kMethodNotFound:
      return "Method not found";
    case LspErrorCode::kInvalidParams:
      return "Invalid params";
    case LspErrorCode::kInternalError:
      return "Internal error";
    case LspErrorCode::kServerError:
      return "Server error";
    case LspErrorCode::kTransportError:
      return "Transport error";
    case LspErrorCode::kTimeoutError:
      return "Timeout error";
    case LspErrorCode::kClientError:
      return "Client error";
    case LspErrorCode::kMethodNotImplemented:
      return "Method not implemented";
    case LspErrorCode::kDocumentNotOpen:
      return "Document not open";
    case LspErrorCode::kDocumentNotFound:
      return "Document not found";
    case LspErrorCode::kUnknownError:
      return "Unknown error";
  }
}
}  // namespace detail

class LspError {
 public:
  explicit LspError(LspErrorCode code, std::string message)
      : code_(code), message_(std::move(message)) {
  }

  [[nodiscard]] auto Code() const -> LspErrorCode {
    return code_;
  }
  [[nodiscard]] auto Message() const -> const std::string& {
    return message_;
  }
  [[nodiscard]] auto ToJson() const -> nlohmann::json {
    return {
        {"code", code_},
        {"message", message_},
    };
  }

  static auto FromCode(LspErrorCode code, const std::string& message = "")
      -> LspError {
    if (message.empty()) {
      return LspError(code, detail::DefaultMessageFor(code));
    }
    return LspError(code, message);
  }

  static auto UnexpectedFromCode(
      LspErrorCode code, const std::string& details = "")
      -> std::unexpected<LspError> {
    return std::unexpected<LspError>(FromCode(code, details));
  }

  static auto FromRpcError(const RpcError& error) -> LspError {
    LspErrorCode code{};
    switch (error.Code()) {
      case RpcErrorCode::kParseError:
        code = LspErrorCode::kParseError;
        break;
      case RpcErrorCode::kInvalidRequest:
        code = LspErrorCode::kInvalidRequest;
        break;
      case RpcErrorCode::kMethodNotFound:
        code = LspErrorCode::kMethodNotFound;
        break;
      case RpcErrorCode::kInvalidParams:
        code = LspErrorCode::kInvalidParams;
        break;
      case RpcErrorCode::kInternalError:
        code = LspErrorCode::kInternalError;
        break;
      case RpcErrorCode::kServerError:
        code = LspErrorCode::kServerError;
        break;
      case RpcErrorCode::kTransportError:
        code = LspErrorCode::kTransportError;
        break;
      case RpcErrorCode::kTimeoutError:
        code = LspErrorCode::kTimeoutError;
        break;
      case RpcErrorCode::kClientError:
        code = LspErrorCode::kClientError;
        break;
      default:
        code = LspErrorCode::kUnknownError;
        break;
    }
    return LspError(code, error.Message());
  }

  static auto UnexpectedFromRpcError(const RpcError& error)
      -> std::unexpected<LspError> {
    return std::unexpected<LspError>(FromRpcError(error));
  }

 private:
  LspErrorCode code_;
  std::string message_;
};

inline auto Ok() -> std::expected<void, LspError> {
  return {};
}

inline void to_json(nlohmann::json& j, const LspError& e) {
  j = e.ToJson();
}

}  // namespace lsp::error

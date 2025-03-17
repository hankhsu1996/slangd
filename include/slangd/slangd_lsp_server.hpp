#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

#include <asio/awaitable.hpp>
#include <asio/io_context.hpp>
#include <asio/strand.hpp>

#include "lsp/server.hpp"
#include "slangd/document_manager.hpp"

namespace slangd {

// Forward declaration
class TestSlangdLspServer;

/**
 * SystemVerilog symbol types mapped to LSP SymbolKind values
 */
enum class SymbolType {
  Module = 3,
  Interface = 11,
  Parameter = 13,
  Variable = 13,
  Task = 12,
  Function = 12,
  ModuleInstance = 6,
  Unknown = 0
};

/**
 * Represents a SystemVerilog symbol with its location
 */
struct Symbol {
  std::string name;
  SymbolType type = SymbolType::Unknown;
  std::string uri;
  int line = 0;
  int character = 0;
  std::string documentation;
};

/**
 * SystemVerilog Language Server implementing the LSP protocol
 */
class SlangdLspServer : public lsp::Server {
 public:
  /** Constructor initializes the server with an io_context. */
  SlangdLspServer(asio::io_context& io_context);

  /** Virtual destructor for proper cleanup. */
  virtual ~SlangdLspServer();

  /** Shutdown the server cleanly. */
  void Shutdown() override;

  /** Register all LSP message handlers with the JSON-RPC endpoint. */
  void RegisterHandlers() override;

 private:
  /** TestSlangdLspServer is given access to private members for testing. */
  friend class TestSlangdLspServer;

  /** Handle "initialize" request from client. */
  asio::awaitable<nlohmann::json> HandleInitialize(
      const std::optional<nlohmann::json>& params);

  /** Handle "shutdown" request from client. */
  asio::awaitable<nlohmann::json> HandleShutdown(
      const std::optional<nlohmann::json>& params);

  /** Handle "initialized" notification from client. */
  asio::awaitable<void> HandleInitialized(
      const std::optional<nlohmann::json>& params);

  /** Handle "exit" notification from client. */
  asio::awaitable<void> HandleExit(const std::optional<nlohmann::json>& params);

  /** Handle "textDocument/didOpen" notification. */
  asio::awaitable<void> HandleTextDocumentDidOpen(
      const std::optional<nlohmann::json>& params);

  /** Handle "textDocument/didChange" notification. */
  asio::awaitable<void> HandleTextDocumentDidChange(
      const std::optional<nlohmann::json>& params);

  /** Handle "textDocument/didClose" notification. */
  asio::awaitable<void> HandleTextDocumentDidClose(
      const std::optional<nlohmann::json>& params);

  /** Index the workspace for SystemVerilog files and symbols. */
  asio::awaitable<void> IndexWorkspace();

  /** Index a single file for symbols. */
  asio::awaitable<void> IndexFile(
      const std::string& uri, const std::string& content);

  /** Parse a SystemVerilog file and report errors. */
  asio::awaitable<std::expected<void, ParseError>> ParseFile(
      const std::string& uri, const std::string& content);

  /** Extract symbols from a parsed file. */
  asio::awaitable<void> ExtractSymbols(const std::string& uri);

  // Server state
  bool initialized_ = false;
  bool shutdown_requested_ = false;
  bool should_exit_ = false;
  int exit_code_ = 0;
  bool indexing_complete_ = false;

  // Thread safety
  asio::strand<asio::io_context::executor_type> strand_;

  // Document management
  std::unique_ptr<DocumentManager> document_manager_;

  // Symbol storage
  std::unordered_map<std::string, Symbol> global_symbols_;
};

}  // namespace slangd

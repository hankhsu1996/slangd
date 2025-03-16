#pragma once

#include <memory>
#include <optional>
#include <slang/syntax/SyntaxTree.h>
#include <string>
#include <unordered_map>

#include <asio.hpp>

#include "lsp/server.hpp"
#include "slangd/document_manager.hpp"

namespace slangd {

/**
 * @brief SystemVerilog symbol types
 */
enum class SymbolType {
  Module,
  Package,
  Interface,
  Class,
  Typedef,
  Function,
  Task,
  Variable,
  Parameter,
  Port,
  Unknown
};

/**
 * @brief Basic representation of a SystemVerilog symbol
 */
struct Symbol {
  std::string name;
  SymbolType type;
  std::string uri;
  int line;
  int character;
  std::string documentation;
};

/**
 * @brief SystemVerilog Language Server implementation
 */
class SlangdLspServer : public lsp::Server {
 public:
  SlangdLspServer(asio::io_context& io_context);
  ~SlangdLspServer() override;

  /**
   * @brief Override shutdown to handle LSP shutdown/exit protocol
   */
  void Shutdown() override;

 protected:
  /**
   * @brief Register SystemVerilog-specific LSP method handlers with the
   * JSON-RPC endpoint
   *
   * Implements the registration of all LSP protocol handlers following the
   * delegation pattern, where each JSON-RPC method delegates to a corresponding
   * handler method that takes JSON parameters and returns an awaitable result.
   */
  void RegisterHandlers() override;

 private:
  // Simplified handler methods without complex return types
  void OnInitialize();
  void OnTextDocumentDidOpen();
  void OnTextDocumentCompletion();

  // LSP protocol handler implementations (new architecture)
  /**
   * @brief Handle LSP initialize request
   * @param params JSON-RPC parameters containing client capabilities
   * @return awaitable with server capabilities
   */
  asio::awaitable<nlohmann::json> HandleInitialize(
      const std::optional<nlohmann::json>& params);

  /**
   * @brief Handle LSP shutdown request
   * @param params JSON-RPC parameters (unused for shutdown)
   * @return awaitable with null result as per LSP spec
   */
  asio::awaitable<nlohmann::json> HandleShutdown(
      const std::optional<nlohmann::json>& params);

  /**
   * @brief Handle LSP initialized notification
   * @param params JSON-RPC parameters (unused for initialized)
   * @return awaitable with void
   */
  asio::awaitable<void> HandleInitialized(
      const std::optional<nlohmann::json>& params);

  /**
   * @brief Handle LSP exit notification
   * @param params JSON-RPC parameters (unused for exit)
   * @return awaitable with void
   */
  asio::awaitable<void> HandleExit(const std::optional<nlohmann::json>& params);

  /**
   * @brief Handle textDocument/didOpen notification
   * @param params JSON-RPC parameters containing document URI, text, and
   * language ID
   * @return awaitable with void
   */
  asio::awaitable<void> HandleTextDocumentDidOpen(
      const std::optional<nlohmann::json>& params);

  /**
   * @brief Handle textDocument/didChange notification
   * @param params JSON-RPC parameters containing document URI and changes
   * @return awaitable with void
   */
  asio::awaitable<void> HandleTextDocumentDidChange(
      const std::optional<nlohmann::json>& params);

  /**
   * @brief Handle textDocument/didClose notification
   * @param params JSON-RPC parameters containing document URI
   * @return awaitable with void
   */
  asio::awaitable<void> HandleTextDocumentDidClose(
      const std::optional<nlohmann::json>& params);

  /**
   * @brief Handle textDocument/hover request
   * @param params JSON-RPC parameters containing document URI and position
   * @return awaitable with hover result JSON
   */
  asio::awaitable<nlohmann::json> HandleTextDocumentHover(
      const std::optional<nlohmann::json>& params);

  /**
   * @brief Handle textDocument/definition request
   * @param params JSON-RPC parameters containing document URI and position
   * @return awaitable with definition locations JSON
   */
  asio::awaitable<nlohmann::json> HandleTextDocumentDefinition(
      const std::optional<nlohmann::json>& params);

  /**
   * @brief Handle textDocument/completion request
   * @param params JSON-RPC parameters containing document URI and position
   * @return awaitable with completion items JSON
   */
  asio::awaitable<nlohmann::json> HandleTextDocumentCompletion(
      const std::optional<nlohmann::json>& params);

  /**
   * @brief Handle workspace/symbol request
   * @param params JSON-RPC parameters containing query string
   * @return awaitable with symbol results JSON
   */
  asio::awaitable<nlohmann::json> HandleWorkspaceSymbol(
      const std::optional<nlohmann::json>& params);

  // SystemVerilog-specific methods
  asio::awaitable<void> IndexWorkspace();
  asio::awaitable<void> IndexFile(
      const std::string& uri, const std::string& content);
  asio::awaitable<std::optional<Symbol>> FindSymbol(const std::string& name);
  asio::awaitable<std::vector<Symbol>> FindSymbols(const std::string& query);

  // SystemVerilog parser interface using DocumentManager
  asio::awaitable<std::expected<void, ParseError>> ParseFile(
      const std::string& uri, const std::string& content);
  asio::awaitable<void> ExtractSymbols(const std::string& uri);

 private:
  // Global symbol table for SystemVerilog symbols
  std::unordered_map<std::string, Symbol> global_symbols_;

  // Strand for synchronized access to global_symbols_
  asio::strand<asio::io_context::executor_type> strand_;

  // Flag to track if indexing is complete
  bool indexing_complete_ = false;

  // Document manager for handling SystemVerilog files
  std::unique_ptr<DocumentManager> document_manager_;

  // LSP protocol state flags
  bool shutdown_requested_ = false;  // True if shutdown was called
  bool should_exit_ = false;         // True if exit was called
  int exit_code_ = 0;                // Exit code for the process
  bool initialized_ = false;  // True if initialized notification was received
};

}  // namespace slangd

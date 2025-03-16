#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include <asio.hpp>

// Include slang library headers
#include <slang/syntax/SyntaxTree.h>
#include <slang/text/SourceManager.h>

#include "lsp/server.hpp"

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
  SlangdLspServer();
  ~SlangdLspServer() override;

 protected:
  /**
   * @brief Register SystemVerilog-specific LSP method handlers
   */
  void RegisterHandlers() override;

  // Override base class handlers with SystemVerilog-specific implementations
  void HandleInitialize() override;
  void HandleTextDocumentDidOpen(const std::string& uri,
                                 const std::string& text,
                                 const std::string& language_id) override;
  void HandleTextDocumentHover(const std::string& uri, int line,
                               int character) override;
  void HandleTextDocumentDefinition(const std::string& uri, int line,
                                    int character) override;
  void HandleTextDocumentCompletion(const std::string& uri, int line,
                                    int character) override;
  void HandleWorkspaceSymbol(const std::string& query) override;

 private:
  // Simplified handler methods without complex return types
  void OnInitialize();
  void OnTextDocumentDidOpen();
  void OnTextDocumentCompletion();

  // SystemVerilog-specific methods
  void IndexWorkspace();
  void IndexFile(const std::string& uri, const std::string& content);
  Symbol* FindSymbol(const std::string& name);
  std::vector<Symbol> FindSymbols(const std::string& query);

  // SystemVerilog parser interface
  void ParseFile(const std::string& uri, const std::string& content);
  void ExtractSymbols(const std::string& uri, const std::string& content);

 private:
  // Global symbol table for SystemVerilog symbols
  std::unordered_map<std::string, Symbol> global_symbols_;

  // Mutex for thread-safe access to global_symbols_
  std::mutex symbols_mutex_;

  // Strand for synchronized access to global_symbols_
  asio::strand<asio::io_context::executor_type> strand_;

  // Flag to track if indexing is complete
  bool indexing_complete_ = false;

  // Slang library components
  std::unique_ptr<slang::SourceManager> source_manager_;
  std::unordered_map<std::string, std::shared_ptr<slang::syntax::SyntaxTree>>
      syntax_trees_;
};

}  // namespace slangd

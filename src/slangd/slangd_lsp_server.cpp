#include "slangd/slangd_lsp_server.hpp"

#include <chrono>
#include <slang/syntax/AllSyntax.h>
#include <slang/syntax/SyntaxVisitor.h>
#include <thread>

namespace slangd {

SlangdLspServer::SlangdLspServer(asio::io_context& io_context)
    : lsp::Server(io_context), strand_(asio::make_strand(io_context)) {
  // Initialize the document manager with a reference to io_context
  document_manager_ = std::make_unique<DocumentManager>(io_context);
}

SlangdLspServer::~SlangdLspServer() {}

void SlangdLspServer::RegisterHandlers() {
  // Register standard LSP methods
  RegisterMethod("initialize", nullptr);
  RegisterMethod("textDocument/didOpen", nullptr);
  RegisterMethod("textDocument/hover", nullptr);
  RegisterMethod("textDocument/definition", nullptr);
  RegisterMethod("textDocument/completion", nullptr);
  RegisterMethod("workspace/symbol", nullptr);
}

// LSP method handlers
void SlangdLspServer::HandleInitialize() {
  // Start workspace indexing in a separate thread
  asio::co_spawn(
      strand_, [this]() -> asio::awaitable<void> { co_await IndexWorkspace(); },
      asio::detached);
}

void SlangdLspServer::HandleTextDocumentDidOpen(
    const std::string& uri, const std::string& text,
    const std::string& language_id) {
  // Call base implementation to manage open_files_
  lsp::Server::HandleTextDocumentDidOpen(uri, text, language_id);

  // Post to strand to ensure thread safety
  asio::co_spawn(
      strand_,
      [this, uri, text]() -> asio::awaitable<void> {
        // Parse the file and extract symbols
        co_await ParseFile(uri, text);
        co_await ExtractSymbols(uri);
      },
      asio::detached);
}

void SlangdLspServer::HandleTextDocumentHover(
    const std::string& uri, int line, int character) {
  // First check in open files (lightweight operation)
  auto* file = GetOpenFile(uri);
  if (!file) {
    return;
  }

  // In real implementation, would look up the symbol at the given position
  // and return its documentation
}

void SlangdLspServer::HandleTextDocumentDefinition(
    const std::string& uri, int line, int character) {
  // First check in open files (lightweight operation)
  auto* file = GetOpenFile(uri);
  if (!file) {
    return;
  }

  // In real implementation, would look up the symbol at the given position
  // and return its definition location
}

void SlangdLspServer::HandleTextDocumentCompletion(
    const std::string& uri, int line, int character) {
  // This is a more complex operation, so post it to the thread pool via strand
  asio::co_spawn(
      strand_,
      [this, uri]() -> asio::awaitable<void> {
        // First check in open files
        auto* file = GetOpenFile(uri);
        if (!file) {
          co_return;
        }

        // In real implementation, would gather completion items based on
        // context
        co_return;
      },
      asio::detached);
}

void SlangdLspServer::HandleWorkspaceSymbol(const std::string& query) {
  // This is a more complex operation, so post it to the thread pool via strand
  asio::co_spawn(
      strand_,
      [this, query]() -> asio::awaitable<void> {
        auto symbols = co_await FindSymbols(query);
        co_return;
      },
      asio::detached);
}

// Legacy methods (simplified handlers from original skeleton)
void SlangdLspServer::OnInitialize() { HandleInitialize(); }

void SlangdLspServer::OnTextDocumentDidOpen() {}

void SlangdLspServer::OnTextDocumentCompletion() {}

// SystemVerilog-specific methods
asio::awaitable<void> SlangdLspServer::IndexFile(
    const std::string& uri, const std::string& content) {
  // Use the document manager to handle the file
  co_await ParseFile(uri, content);
  co_await ExtractSymbols(uri);
}

asio::awaitable<void> SlangdLspServer::IndexWorkspace() {
  // Simulating indexing work
  std::this_thread::sleep_for(std::chrono::seconds(1));

  // In real implementation, would recursively find and parse all .sv files
  indexing_complete_ = true;
  co_return;
}

asio::awaitable<void> SlangdLspServer::ParseFile(
    const std::string& uri, const std::string& content) {
  // Use the DocumentManager to handle parsing
  co_await document_manager_->ParseDocument(uri, content);
  co_return;
}

asio::awaitable<void> SlangdLspServer::ExtractSymbols(const std::string& uri) {
  // Get the syntax tree from the document manager
  auto syntax_tree = co_await document_manager_->GetSyntaxTree(uri);
  if (!syntax_tree) {
    co_return;
  }

  // Switch to the strand for synchronized access to global_symbols_
  co_await asio::post(strand_, asio::use_awaitable);

  // In this simplified version, just create some example symbols
  // Add a dummy module symbol
  std::string module_name = "example_module";
  Symbol module;
  module.name = module_name;
  module.type = SymbolType::Module;
  module.uri = uri;
  module.line = 1;
  module.character = 0;
  module.documentation = "Example SystemVerilog module";
  global_symbols_[module_name] = module;

  // Add a dummy interface symbol
  std::string iface_name = "example_interface";
  Symbol iface;
  iface.name = iface_name;
  iface.type = SymbolType::Interface;
  iface.uri = uri;
  iface.line = 10;
  iface.character = 0;
  iface.documentation = "Example SystemVerilog interface";
  global_symbols_[iface_name] = iface;
  co_return;
}

asio::awaitable<std::optional<Symbol>> SlangdLspServer::FindSymbol(
    const std::string& name) {
  // Switch to the strand for synchronized access to global_symbols_
  co_await asio::post(strand_, asio::use_awaitable);

  auto it = global_symbols_.find(name);
  if (it != global_symbols_.end()) {
    co_return it->second;
  }
  co_return std::nullopt;
}

asio::awaitable<std::vector<Symbol>> SlangdLspServer::FindSymbols(
    const std::string& query) {
  // Switch to the strand for synchronized access to global_symbols_
  co_await asio::post(strand_, asio::use_awaitable);

  std::vector<Symbol> results;

  // Simple substring search (in real implementation would use fuzzy matching)
  for (const auto& [name, symbol] : global_symbols_) {
    if (name.find(query) != std::string::npos) {
      results.push_back(symbol);
    }
  }

  co_return results;
}

}  // namespace slangd

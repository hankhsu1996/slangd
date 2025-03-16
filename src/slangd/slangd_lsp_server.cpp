#include "slangd/slangd_lsp_server.hpp"

#include <chrono>
#include <iostream>
#include <thread>

// Additional slang headers
#include <slang/syntax/AllSyntax.h>
#include <slang/syntax/SyntaxVisitor.h>

namespace slangd {

SlangdLspServer::SlangdLspServer() : strand_(asio::make_strand(*io_context_)) {
  std::cout << "SlangdLspServer created" << std::endl;

  // Initialize the slang source manager
  source_manager_ = std::make_unique<slang::SourceManager>();
}

SlangdLspServer::~SlangdLspServer() {
  std::cout << "SlangdLspServer destroyed" << std::endl;
}

void SlangdLspServer::RegisterHandlers() {
  std::cout << "Registering handlers for SystemVerilog LSP" << std::endl;

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
  std::cout << "SystemVerilog LSP initialized" << std::endl;

  // Start workspace indexing in a separate thread
  asio::post(strand_, [this]() { IndexWorkspace(); });
}

void SlangdLspServer::HandleTextDocumentDidOpen(
    const std::string& uri, const std::string& text,
    const std::string& language_id) {
  // Call base implementation to manage open_files_
  lsp::Server::HandleTextDocumentDidOpen(uri, text, language_id);

  // Post to strand to ensure thread safety
  asio::post(strand_, [this, uri, text]() {
    std::cout << "Parsing SystemVerilog file: " << uri << std::endl;
    ParseFile(uri, text);
    ExtractSymbols(uri, text);
  });
}

void SlangdLspServer::HandleTextDocumentHover(const std::string& uri, int line,
                                              int character) {
  std::cout << "SystemVerilog hover request at " << uri << ":" << line << ":"
            << character << std::endl;

  // First check in open files (lightweight operation)
  auto* file = GetOpenFile(uri);
  if (!file) {
    std::cout << "File not open: " << uri << std::endl;
    return;
  }

  // In real implementation, would look up the symbol at the given position
  // and return its documentation
}

void SlangdLspServer::HandleTextDocumentDefinition(const std::string& uri,
                                                   int line, int character) {
  std::cout << "SystemVerilog definition request at " << uri << ":" << line
            << ":" << character << std::endl;

  // First check in open files (lightweight operation)
  auto* file = GetOpenFile(uri);
  if (!file) {
    std::cout << "File not open: " << uri << std::endl;
    return;
  }

  // In real implementation, would look up the symbol at the given position
  // and return its definition location
}

void SlangdLspServer::HandleTextDocumentCompletion(const std::string& uri,
                                                   int line, int character) {
  std::cout << "SystemVerilog completion request at " << uri << ":" << line
            << ":" << character << std::endl;

  // This is a more complex operation, so post it to the thread pool via strand
  asio::post(strand_, [this, uri]() {
    // First check in open files
    auto* file = GetOpenFile(uri);
    if (!file) {
      std::cout << "File not open: " << uri << std::endl;
      return;
    }

    // In real implementation, would gather completion items based on context
    std::cout << "Completion items would be generated here" << std::endl;
  });
}

void SlangdLspServer::HandleWorkspaceSymbol(const std::string& query) {
  std::cout << "SystemVerilog workspace symbol request for: " << query
            << std::endl;

  // This is a more complex operation, so post it to the thread pool via strand
  asio::post(strand_, [this, query]() {
    auto symbols = FindSymbols(query);
    std::cout << "Found " << symbols.size() << " symbols matching '" << query
              << "'" << std::endl;
  });
}

// Legacy methods (simplified handlers from original skeleton)
void SlangdLspServer::OnInitialize() {
  std::cout << "Legacy initialize request received" << std::endl;
  HandleInitialize();
}

void SlangdLspServer::OnTextDocumentDidOpen() {
  std::cout << "Legacy document opened" << std::endl;
}

void SlangdLspServer::OnTextDocumentCompletion() {
  std::cout << "Legacy completion request received" << std::endl;
}

// SystemVerilog-specific methods
void SlangdLspServer::IndexWorkspace() {
  std::cout << "Starting workspace indexing" << std::endl;

  // Simulating indexing work
  std::this_thread::sleep_for(std::chrono::seconds(1));

  // In real implementation, would recursively find and parse all .sv files

  std::cout << "Workspace indexing complete" << std::endl;
  indexing_complete_ = true;
}

void SlangdLspServer::IndexFile(const std::string& uri,
                                const std::string& content) {
  std::cout << "Indexing file: " << uri << std::endl;
  ParseFile(uri, content);
  ExtractSymbols(uri, content);
}

Symbol* SlangdLspServer::FindSymbol(const std::string& name) {
  std::lock_guard<std::mutex> lock(symbols_mutex_);
  auto it = global_symbols_.find(name);
  if (it != global_symbols_.end()) {
    return &it->second;
  }
  return nullptr;
}

std::vector<Symbol> SlangdLspServer::FindSymbols(const std::string& query) {
  std::vector<Symbol> results;
  std::lock_guard<std::mutex> lock(symbols_mutex_);

  // Simple substring search (in real implementation would use fuzzy matching)
  for (const auto& [name, symbol] : global_symbols_) {
    if (name.find(query) != std::string::npos) {
      results.push_back(symbol);
    }
  }

  return results;
}

void SlangdLspServer::ParseFile(const std::string& uri,
                                const std::string& content) {
  std::cout << "Parsing SystemVerilog file: " << uri << std::endl;

  try {
    // Use slang directly to parse SystemVerilog content
    auto syntax_tree =
        slang::syntax::SyntaxTree::fromText(content, *source_manager_, uri);

    if (!syntax_tree) {
      std::cerr << "Failed to parse file: " << uri << std::endl;
      return;
    }

    // Store the syntax tree for future reference
    syntax_trees_[uri] = syntax_tree;

    std::cout << "Successfully parsed file: " << uri << std::endl;
  } catch (const std::exception& e) {
    std::cerr << "Exception parsing file " << uri << ": " << e.what()
              << std::endl;
  }
}

void SlangdLspServer::ExtractSymbols(const std::string& uri,
                                     const std::string& /*content*/) {
  std::cout << "Extracting symbols from: " << uri << std::endl;

  // In this simplified version, just create some example symbols
  // In a real implementation, we would traverse the syntax tree to find symbols

  std::lock_guard<std::mutex> lock(symbols_mutex_);

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

  std::cout << "Added " << global_symbols_.size()
            << " symbols to global symbol table" << std::endl;
}

}  // namespace slangd

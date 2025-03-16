#include "slangd/slangd_lsp_server.hpp"

#include <chrono>
#include <iostream>
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

void SlangdLspServer::Shutdown() {
  std::cout << "SlangdLspServer shutting down" << std::endl;

  // We no longer need to manage shutdown state here, as it's done in
  // HandleShutdown

  // Call base class implementation to clean up resources
  lsp::Server::Shutdown();

  // Check exit state set by HandleExit
  if (should_exit_) {
    std::cout << "Ready to exit with code: " << exit_code_ << std::endl;
  }
}

void SlangdLspServer::RegisterHandlers() {
  // Register standard LSP methods with proper handler implementation
  endpoint_->RegisterMethodCall(
      "initialize",
      [this](const std::optional<nlohmann::json>& params)
          -> asio::awaitable<nlohmann::json> {
        // Delegate to the handler method
        return HandleInitialize(params);
      });

  // Add shutdown handler
  endpoint_->RegisterMethodCall(
      "shutdown",
      [this](const std::optional<nlohmann::json>& params)
          -> asio::awaitable<nlohmann::json> {
        // Delegate to the handler method
        return HandleShutdown(params);
      });

  // Add exit notification handler
  endpoint_->RegisterNotification(
      "exit",
      [this](const std::optional<nlohmann::json>& params)
          -> asio::awaitable<void> {
        // Delegate to the handler method
        return HandleExit(params);
      });

  // Add initialized notification handler
  endpoint_->RegisterNotification(
      "initialized",
      [this](const std::optional<nlohmann::json>& params)
          -> asio::awaitable<void> {
        // Delegate to the handler method
        return HandleInitialized(params);
      });

  endpoint_->RegisterMethodCall(
      "textDocument/hover",
      [this](const std::optional<nlohmann::json>& params)
          -> asio::awaitable<nlohmann::json> {
        return HandleTextDocumentHover(params);
      });

  endpoint_->RegisterMethodCall(
      "textDocument/definition",
      [this](const std::optional<nlohmann::json>& params)
          -> asio::awaitable<nlohmann::json> {
        return HandleTextDocumentDefinition(params);
      });

  endpoint_->RegisterMethodCall(
      "textDocument/completion",
      [this](const std::optional<nlohmann::json>& params)
          -> asio::awaitable<nlohmann::json> {
        return HandleTextDocumentCompletion(params);
      });

  endpoint_->RegisterMethodCall(
      "workspace/symbol",
      [this](const std::optional<nlohmann::json>& params)
          -> asio::awaitable<nlohmann::json> {
        return HandleWorkspaceSymbol(params);
      });

  // Register notifications
  endpoint_->RegisterNotification(
      "textDocument/didOpen",
      [this](const std::optional<nlohmann::json>& params)
          -> asio::awaitable<void> {
        return HandleTextDocumentDidOpen(params);
      });

  endpoint_->RegisterNotification(
      "textDocument/didChange",
      [this](const std::optional<nlohmann::json>& params)
          -> asio::awaitable<void> {
        return HandleTextDocumentDidChange(params);
      });

  endpoint_->RegisterNotification(
      "textDocument/didClose",
      [this](const std::optional<nlohmann::json>& params)
          -> asio::awaitable<void> {
        return HandleTextDocumentDidClose(params);
      });
}

asio::awaitable<nlohmann::json> SlangdLspServer::HandleInitialize(
    const std::optional<nlohmann::json>& params) {
  // Start workspace indexing in a separate thread
  asio::co_spawn(
      strand_, [this]() -> asio::awaitable<void> { co_await IndexWorkspace(); },
      asio::detached);

  // Return initialize result
  nlohmann::json capabilities = {
      {"textDocumentSync", 1},  // 1 = Full sync
      {"hoverProvider", true},
      {"definitionProvider", true},
      {"completionProvider", {{"triggerCharacters", {"."}}}},
      {"workspaceSymbolProvider", true}};

  nlohmann::json result = {
      {"capabilities", capabilities},
      {"serverInfo", {{"name", "slangd"}, {"version", "0.1.0"}}}};

  co_return result;
}

asio::awaitable<nlohmann::json> SlangdLspServer::HandleTextDocumentHover(
    const std::optional<nlohmann::json>& params) {
  if (!params) {
    co_return nullptr;
  }

  const auto& param_val = params.value();
  const auto& uri = param_val["textDocument"]["uri"].get<std::string>();
  const auto& position = param_val["position"];
  [[maybe_unused]] const int line = position["line"].get<int>();
  [[maybe_unused]] const int character = position["character"].get<int>();

  // Check if the file is open
  auto file_opt = GetOpenFile(uri);
  if (!file_opt) {
    co_return nullptr;  // Return null for not found
  }

  // Post to strand for thread safety
  co_await asio::post(strand_, asio::use_awaitable);

  // In a real implementation, look up the symbol at the given position
  // For now, return a placeholder hover response
  nlohmann::json hover_response = {
      {"contents",
       {{"kind", "markdown"},
        {"value",
         "**SystemVerilog Entity**\n\nThis is a placeholder hover "
         "response. In a complete implementation, this would show "
         "documentation for the symbol at the current position."}}},
      {"range",
       {{"start", {{"line", line}, {"character", character}}},
        {"end", {{"line", line}, {"character", character + 5}}}}}};

  co_return hover_response;
}

asio::awaitable<nlohmann::json> SlangdLspServer::HandleTextDocumentDefinition(
    const std::optional<nlohmann::json>& params) {
  if (!params) {
    co_return nullptr;
  }

  const auto& param_val = params.value();
  const auto& uri = param_val["textDocument"]["uri"].get<std::string>();
  const auto& position = param_val["position"];
  [[maybe_unused]] const int line = position["line"].get<int>();
  [[maybe_unused]] const int character = position["character"].get<int>();

  // Check if the file is open
  auto file_opt = GetOpenFile(uri);
  if (!file_opt) {
    co_return nullptr;  // Return null for not found
  }

  // Post to strand for thread safety
  co_await asio::post(strand_, asio::use_awaitable);

  // In a real implementation, we would look up the symbol definition
  // For now, return a dummy location (back to the same position)
  nlohmann::json location = {
      {"uri", uri},
      {"range",
       {{"start", {{"line", line}, {"character", character}}},
        {"end", {{"line", line}, {"character", character + 5}}}}}};

  // Return an array with one location
  co_return nlohmann::json::array({location});
}

asio::awaitable<nlohmann::json> SlangdLspServer::HandleTextDocumentCompletion(
    const std::optional<nlohmann::json>& params) {
  if (!params) {
    co_return nullptr;
  }

  const auto& param_val = params.value();
  const auto& uri = param_val["textDocument"]["uri"].get<std::string>();
  const auto& position = param_val["position"];
  [[maybe_unused]] const int line = position["line"].get<int>();
  [[maybe_unused]] const int character = position["character"].get<int>();

  // Check if the file is open
  auto file_opt = GetOpenFile(uri);
  if (!file_opt) {
    co_return nullptr;  // Return null for not found
  }

  // Post to strand for thread safety
  co_await asio::post(strand_, asio::use_awaitable);

  // In a real implementation, we would compute context-aware completions
  // For now, return some dummy completion items
  nlohmann::json completions = {
      {"isIncomplete", false},
      {"items", nlohmann::json::array(
                    {{{"label", "module"},
                      {"kind", 14},  // 14 = Keyword
                      {"detail", "SystemVerilog module keyword"},
                      {"insertText",
                       "module ${1:name} (\n\t${2}\n);\n\t${0}\nendmodule"}},
                     {{"label", "if"},
                      {"kind", 14},  // 14 = Keyword
                      {"detail", "SystemVerilog if statement"},
                      {"insertText", "if (${1:condition}) begin\n\t${0}\nend"}},
                     {{"label", "always_comb"},
                      {"kind", 14},  // 14 = Keyword
                      {"detail", "SystemVerilog always_comb block"},
                      {"insertText", "always_comb begin\n\t${0}\nend"}}})}};

  co_return completions;
}

asio::awaitable<nlohmann::json> SlangdLspServer::HandleWorkspaceSymbol(
    const std::optional<nlohmann::json>& params) {
  if (!params) {
    co_return nlohmann::json::array();
  }

  const auto& param_val = params.value();
  const auto& query = param_val["query"].get<std::string>();

  // Post to strand for thread safety
  co_await asio::post(strand_, asio::use_awaitable);

  // Find symbols matching the query
  auto symbols = co_await FindSymbols(query);

  // Convert to LSP format
  nlohmann::json symbol_results = nlohmann::json::array();
  for (const auto& symbol : symbols) {
    symbol_results.push_back(
        {{"name", symbol.name},
         {"kind", static_cast<int>(
                      symbol.type)},  // Convert SymbolType to LSP SymbolKind
         {"location",
          {{"uri", symbol.uri},
           {"range",
            {{"start",
              {{"line", symbol.line}, {"character", symbol.character}}},
             {"end",
              {{"line", symbol.line},
               {"character",
                symbol.character +
                    static_cast<int>(symbol.name.length())}}}}}}}});
  }

  co_return symbol_results;
}

asio::awaitable<void> SlangdLspServer::IndexFile(
    const std::string& uri, const std::string& content) {
  // Use the document manager to handle the file
  auto parse_result = co_await ParseFile(uri, content);
  if (parse_result) {  // Only extract symbols if parsing succeeded
    co_await ExtractSymbols(uri);
  }
}

asio::awaitable<void> SlangdLspServer::IndexWorkspace() {
  // Simulating indexing work
  std::this_thread::sleep_for(std::chrono::seconds(1));

  // In real implementation, would recursively find and parse all .sv files
  indexing_complete_ = true;
  co_return;
}

asio::awaitable<std::expected<void, ParseError>> SlangdLspServer::ParseFile(
    const std::string& uri, const std::string& content) {
  // Forward the DocumentManager's parse result directly
  auto parse_result = co_await document_manager_->ParseDocument(uri, content);

  // If there's an error, log it and notify the client before returning
  if (!parse_result) {
    std::string error_message;
    switch (parse_result.error()) {
      case ParseError::SyntaxError:
        error_message = "Syntax error in SystemVerilog file: " + uri;
        break;
      case ParseError::FileNotFound:
        error_message = "File not found: " + uri;
        break;
      case ParseError::EncodingError:
        error_message = "Text encoding error in file: " + uri;
        break;
      case ParseError::SlangInternalError:
        error_message = "Internal error in slang parser for file: " + uri;
        break;
      case ParseError::UnknownError:
      default:
        error_message = "Unknown error parsing file: " + uri;
        break;
    }

    std::cerr << "Parse error: " << error_message << std::endl;

    // Send error message to client as a "window/showMessage" notification
    if (endpoint_) {
      nlohmann::json params = {
          {"type", 1},  // 1 = Error
          {"message", error_message}};

      // Properly co_await the notification instead of ignoring it
      try {
        co_await endpoint_->SendNotification("window/showMessage", params);
      } catch (const std::exception& e) {
        std::cerr << "Failed to send error notification to client: " << e.what()
                  << std::endl;
      }
    }
  }

  // Return the original result with error information preserved
  co_return parse_result;
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

asio::awaitable<nlohmann::json> SlangdLspServer::HandleShutdown(
    const std::optional<nlohmann::json>& params) {
  // Set shutdown flag to indicate server is shutting down
  shutdown_requested_ = true;

  std::cout << "SlangdLspServer shutdown request received" << std::endl;

  // Return empty/null result as per LSP spec
  co_return nullptr;
}

asio::awaitable<void> SlangdLspServer::HandleExit(
    const std::optional<nlohmann::json>& params) {
  // If shutdown was called before, exit with code 0
  // Otherwise, exit with error code 1 as per LSP spec
  int exit_code = shutdown_requested_ ? 0 : 1;

  // Note: We can't actually exit the process here as it would be abrupt
  // Instead, we'll set a flag and let the main loop handle the exit
  should_exit_ = true;
  exit_code_ = exit_code;

  std::cout
      << "SlangdLspServer exit notification received, will exit with code: "
      << exit_code << std::endl;

  // Signal io_context to stop
  io_context_.stop();

  co_return;
}

asio::awaitable<void> SlangdLspServer::HandleInitialized(
    const std::optional<nlohmann::json>& params) {
  // Mark server as initialized
  initialized_ = true;

  std::cout << "SlangdLspServer initialized notification received" << std::endl;

  // This would be a good place to send server-initiated requests
  // such as workspace configuration requests or client registrations

  co_return;
}

asio::awaitable<void> SlangdLspServer::HandleTextDocumentDidOpen(
    const std::optional<nlohmann::json>& params) {
  if (!params) {
    co_return;
  }

  const auto& param_val = params.value();
  const auto& uri = param_val["textDocument"]["uri"].get<std::string>();
  const auto& text = param_val["textDocument"]["text"].get<std::string>();
  const auto& language_id =
      param_val["textDocument"]["languageId"].get<std::string>();

  // Use file management helpers directly instead of calling
  // HandleTextDocumentDidOpen
  AddOpenFile(uri, text, language_id, 1);
  std::cout << "Document opened: " << uri << std::endl;

  // Post to strand to ensure thread safety
  asio::co_spawn(
      strand_,
      [this, uri, text]() -> asio::awaitable<void> {
        // Parse the file and extract symbols
        auto parse_result = co_await ParseFile(uri, text);
        if (parse_result) {  // Only extract symbols if parsing succeeded
          co_await ExtractSymbols(uri);
        }
      },
      asio::detached);

  co_return;
}

asio::awaitable<void> SlangdLspServer::HandleTextDocumentDidChange(
    const std::optional<nlohmann::json>& params) {
  if (!params) {
    co_return;
  }

  const auto& param_val = params.value();
  const auto& uri = param_val["textDocument"]["uri"].get<std::string>();
  const auto& changes = param_val["contentChanges"];

  // For full sync, we get the full content in the first change
  if (!changes.empty()) {
    const auto& text = changes[0]["text"].get<std::string>();

    // Update the document and re-parse
    auto file_opt = GetOpenFile(uri);
    if (file_opt) {
      lsp::OpenFile& file = file_opt->get();
      file.content = text;
      file.version++;

      // Re-parse the file and extract symbols
      asio::co_spawn(
          strand_,
          [this, uri, text]() -> asio::awaitable<void> {
            auto parse_result = co_await ParseFile(uri, text);
            if (parse_result) {
              co_await ExtractSymbols(uri);
            }
          },
          asio::detached);
    }
  }

  co_return;
}

asio::awaitable<void> SlangdLspServer::HandleTextDocumentDidClose(
    const std::optional<nlohmann::json>& params) {
  if (!params) {
    co_return;
  }

  const auto& param_val = params.value();
  const auto& uri = param_val["textDocument"]["uri"].get<std::string>();

  // Remove from open files directly with RemoveOpenFile
  RemoveOpenFile(uri);

  co_return;
}

}  // namespace slangd

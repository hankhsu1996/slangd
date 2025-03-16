#include "lsp/server.hpp"

#include <iostream>
#include <thread>

#include <asio.hpp>
#include <jsonrpc/endpoint/endpoint.hpp>
#include <jsonrpc/transport/pipe_transport.hpp>
#include <nlohmann/json.hpp>

namespace lsp {

Server::Server(asio::io_context& io_context)
    : io_context_(io_context), work_guard_(asio::make_work_guard(io_context)) {
  std::cout << "Creating LSP server" << std::endl;

  // Create thread pool (4 worker threads by default)
  const unsigned int thread_count = std::thread::hardware_concurrency();
  const unsigned int num_threads = thread_count > 0 ? thread_count : 4;
  std::cout << "Creating thread pool with " << num_threads << " threads"
            << std::endl;
}

// Initialize the JSON-RPC endpoint with a pipe transport
void Server::InitializeJsonRpc() {
  try {
    // For demonstration purposes, use a pipe transport
    // In a real implementation, you might want to create a custom transport for
    // stdin/stdout
    auto transport = std::make_unique<jsonrpc::transport::PipeTransport>(
        io_context_, "/tmp/slangd-lsp-pipe", true);

    // Create the RPC endpoint
    endpoint_ = std::make_unique<jsonrpc::endpoint::RpcEndpoint>(
        io_context_, std::move(transport));

    // Set up error handling
    endpoint_->SetErrorHandler(
        [](jsonrpc::endpoint::ErrorCode code, const std::string& message) {
          std::cerr << "JSON-RPC error: " << message
                    << " (code: " << static_cast<int>(code) << ")" << std::endl;
        });

    std::cout << "JSON-RPC endpoint initialized with pipe transport"
              << std::endl;
  } catch (const std::exception& e) {
    std::cerr << "Error initializing JSON-RPC: " << e.what() << std::endl;
  }
}

Server::~Server() {
  Shutdown();

  // Wait for all threads to complete
  for (auto& thread : thread_pool_) {
    if (thread.joinable()) {
      thread.join();
    }
  }
}

void Server::Run() {
  try {
    // Initialize JSON-RPC endpoint
    InitializeJsonRpc();

    // Register method handlers
    RegisterHandlers();

    // Start worker threads
    for (unsigned int i = 0; i < std::thread::hardware_concurrency(); ++i) {
      thread_pool_.emplace_back([this]() {
        try {
          io_context_.run();
        } catch (const std::exception& e) {
          std::cerr << "Thread exception: " << e.what() << std::endl;
        }
      });
    }

    std::cout << "Starting JSON-RPC endpoint" << std::endl;

    // Start the endpoint asynchronously
    asio::co_spawn(
        io_context_,
        [this]() -> asio::awaitable<void> {
          co_await endpoint_->Start();
          co_await endpoint_->WaitForShutdown();
          co_return;
        },
        asio::detached);

    // Run the IO context in the main thread
    io_context_.run();
  } catch (const std::exception& e) {
    std::cerr << "Error running server: " << e.what() << std::endl;
  }
}

void Server::Shutdown() {
  std::cout << "Server shutting down" << std::endl;

  if (endpoint_) {
    // Shutdown the endpoint asynchronously
    asio::co_spawn(
        io_context_,
        [this]() -> asio::awaitable<void> {
          co_await endpoint_->Shutdown();
          co_return;  // Explicit void return to fix linter warning
        },
        asio::detached);
  }

  // Release work guard to allow threads to finish
  work_guard_.reset();
  io_context_.stop();
}

void Server::RegisterMethod(const std::string& method, void* /*handler_ptr*/) {
  if (!endpoint_) {
    std::cerr << "Cannot register method: endpoint not initialized"
              << std::endl;
    return;
  }

  std::cout << "Registering method: " << method << std::endl;

  // Register a method handler for the given method
  endpoint_->RegisterMethodCall(
      method,
      [method](const std::optional<nlohmann::json>& params)
          -> asio::awaitable<nlohmann::json> {
        std::cout << "Method called: " << method << std::endl;

        // For now, just return an empty object
        // In a real implementation, we would call the actual handler
        co_return nlohmann::json::object();
      });
}

// Core LSP request handlers
void Server::HandleInitialize() {
  std::cout << "Initialize request received" << std::endl;
}

void Server::HandleInitialized() {
  std::cout << "Initialized notification received" << std::endl;
}

void Server::HandleShutdown() {
  std::cout << "Shutdown request received" << std::endl;
  Shutdown();
}

void Server::HandleTextDocumentDidOpen(
    const std::string& uri, const std::string& text,
    const std::string& language_id) {
  std::cout << "Document opened: " << uri << std::endl;
  AddOpenFile(uri, text, language_id, 1);
}

void Server::HandleTextDocumentDidChange(
    const std::string& uri, const std::vector<std::string>& changes) {
  std::cout << "Document changed: " << uri << std::endl;
  UpdateOpenFile(uri, changes);
}

void Server::HandleTextDocumentDidClose(const std::string& uri) {
  std::cout << "Document closed: " << uri << std::endl;
  RemoveOpenFile(uri);
}

void Server::HandleTextDocumentHover(
    const std::string& uri, int line, int character) {
  std::cout << "Hover request at " << uri << ":" << line << ":" << character
            << std::endl;
}

void Server::HandleTextDocumentDefinition(
    const std::string& uri, int line, int character) {
  std::cout << "Definition request at " << uri << ":" << line << ":"
            << character << std::endl;
}

void Server::HandleTextDocumentCompletion(
    const std::string& uri, int line, int character) {
  std::cout << "Completion request at " << uri << ":" << line << ":"
            << character << std::endl;
}

void Server::HandleWorkspaceSymbol(const std::string& query) {
  std::cout << "Workspace symbol request for: " << query << std::endl;
}

// File management helpers
OpenFile* Server::GetOpenFile(const std::string& uri) {
  auto it = open_files_.find(uri);
  if (it != open_files_.end()) {
    return &it->second;
  }
  return nullptr;
}

void Server::AddOpenFile(
    const std::string& uri, const std::string& content,
    const std::string& language_id, int version) {
  OpenFile file{uri, content, language_id, version};
  open_files_[uri] = std::move(file);
}

void Server::UpdateOpenFile(
    const std::string& uri, const std::vector<std::string>& /*changes*/) {
  auto* file = GetOpenFile(uri);
  if (file) {
    // Simplified update - in a real implementation, we would apply the changes
    // Here we just increment the version
    file->version++;
    std::cout << "Updated file " << uri << " to version " << file->version
              << std::endl;
  }
}

void Server::RemoveOpenFile(const std::string& uri) { open_files_.erase(uri); }

}  // namespace lsp

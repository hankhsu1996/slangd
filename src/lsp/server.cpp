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

// File management helpers
std::optional<std::reference_wrapper<OpenFile>> Server::GetOpenFile(
    const std::string& uri) {
  auto it = open_files_.find(uri);
  if (it != open_files_.end()) {
    return std::ref(it->second);
  }
  return std::nullopt;
}

void Server::AddOpenFile(
    const std::string& uri, const std::string& content,
    const std::string& language_id, int version) {
  OpenFile file{uri, content, language_id, version};
  open_files_[uri] = std::move(file);
}

void Server::UpdateOpenFile(
    const std::string& uri, const std::vector<std::string>& /*changes*/) {
  auto file_opt = GetOpenFile(uri);
  if (file_opt) {
    // Simplified update - in a real implementation, we would apply the changes
    // Here we just increment the version
    OpenFile& file = file_opt->get();
    file.version++;
    std::cout << "Updated file " << uri << " to version " << file.version
              << std::endl;
  }
}

void Server::RemoveOpenFile(const std::string& uri) { open_files_.erase(uri); }

}  // namespace lsp

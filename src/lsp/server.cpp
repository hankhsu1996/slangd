#include "lsp/server.hpp"

#include <iostream>

#include <asio.hpp>
#include <jsonrpc/endpoint/endpoint.hpp>
#include <jsonrpc/transport/pipe_transport.hpp>
#include <nlohmann/json.hpp>

namespace lsp {

Server::Server(
    asio::io_context& io_context,
    std::unique_ptr<jsonrpc::endpoint::RpcEndpoint> endpoint)
    : endpoint_(std::move(endpoint)),
      io_context_(io_context),
      work_guard_(asio::make_work_guard(io_context)) {}

Server::~Server() { Shutdown(); }

auto Server::Run() -> asio::awaitable<void> {
  // Register method handlers
  RegisterHandlers();

  // Start the endpoint and wait for shutdown
  co_await endpoint_->Start();
  co_await endpoint_->WaitForShutdown();
}

void Server::Shutdown() {
  std::cout << "Server shutting down" << std::endl;

  if (endpoint_) {
    // Shutdown the endpoint
    asio::co_spawn(
        io_context_,
        [this]() -> asio::awaitable<void> { co_await endpoint_->Shutdown(); },
        // Simple lambda to log errors
        [](std::exception_ptr e) {
          if (e) {
            try {
              std::rethrow_exception(e);
            } catch (const std::exception& ex) {
              std::cerr << "Error in shutdown: " << ex.what() << std::endl;
            }
          }
        });
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

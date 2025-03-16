#include "lsp/server.hpp"

#include <iostream>

#include <asio.hpp>
#include <jsonrpc/endpoint/endpoint.hpp>

namespace lsp {

Server::Server() {
  // Simplified initialization
  io_context_ = std::make_shared<asio::io_context>();

  endpoint_ = nullptr;
  std::cout << "Server created (simplified version)" << std::endl;
}

Server::~Server() = default;

void Server::Run() {
  // Register method handlers
  RegisterHandlers();

  try {
    std::cout << "Server running (simplified implementation)" << std::endl;
    io_context_->run();
  } catch (const std::exception& e) {
    std::cerr << "Error running server: " << e.what() << std::endl;
  }
}

void Server::Shutdown() {
  std::cout << "Server shutting down" << std::endl;
  io_context_->stop();
}

void Server::RegisterMethod(const std::string& method, void* /*handler*/) {
  // Simplified method registration
  std::cout << "Registered method: " << method << std::endl;
  // We're not actually registering anything in this simplified version
}

}  // namespace lsp

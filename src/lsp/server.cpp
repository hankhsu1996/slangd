#include "lsp/server.hpp"

#include <asio.hpp>
#include <jsonrpc/endpoint/endpoint.hpp>
#include <jsonrpc/transport/pipe_transport.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace lsp {

Server::Server(
    asio::io_context& io_context,
    std::unique_ptr<jsonrpc::endpoint::RpcEndpoint> endpoint)
    : endpoint_(std::move(endpoint)),
      io_context_(io_context),
      work_guard_(asio::make_work_guard(io_context)) {}

auto Server::Run() -> asio::awaitable<void> {
  // Register method handlers
  RegisterHandlers();

  // Start the endpoint and wait for shutdown
  co_await endpoint_->Start();
  co_await endpoint_->WaitForShutdown();
}

auto Server::Shutdown() -> asio::awaitable<void> {
  spdlog::info("Server shutting down");

  if (endpoint_) {
    // Directly await the endpoint shutdown
    co_await endpoint_->Shutdown();
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
  spdlog::debug("Adding open file: {}", uri);
  OpenFile file{uri, content, language_id, version};
  open_files_[uri] = std::move(file);
}

void Server::UpdateOpenFile(
    const std::string& uri, const std::vector<std::string>& /*changes*/) {
  spdlog::debug("Updating open file: {}", uri);
  auto file_opt = GetOpenFile(uri);
  if (file_opt) {
    // Simplified update - in a real implementation, we would apply the changes
    // Here we just increment the version
    OpenFile& file = file_opt->get();
    file.version++;
    spdlog::info("Updated file {} to version {}", uri, file.version);
  }
}

void Server::RemoveOpenFile(const std::string& uri) {
  spdlog::debug("Removing open file: {}", uri);
  open_files_.erase(uri);
}

auto Server::PublishDiagnostics(const PublishDiagnosticsParams& params)
    -> asio::awaitable<void> {
  spdlog::debug("Publishing diagnostics for file: {}", params.uri);
  co_await endpoint_->SendNotification(
      "textDocument/publishDiagnostics", nlohmann::json(params));
}

}  // namespace lsp

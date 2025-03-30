#include "lsp/lsp_server.hpp"

#include <asio.hpp>
#include <jsonrpc/endpoint/endpoint.hpp>
#include <jsonrpc/transport/pipe_transport.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace lsp {

LspServer::LspServer(
    asio::any_io_executor executor,
    std::unique_ptr<jsonrpc::endpoint::RpcEndpoint> endpoint)
    : endpoint_(std::move(endpoint)),
      executor_(executor),
      work_guard_(asio::make_work_guard(executor)) {}

auto LspServer::Start() -> asio::awaitable<void> {
  // Register method handlers
  RegisterHandlers();

  // Start the endpoint and wait for shutdown
  co_await endpoint_->Start();
  co_await endpoint_->WaitForShutdown();
}

auto LspServer::Shutdown() -> asio::awaitable<void> {
  spdlog::info("Server shutting down");

  if (endpoint_) {
    // Directly await the endpoint shutdown
    co_await endpoint_->Shutdown();
  }

  // Release work guard to allow threads to finish
  work_guard_.reset();
}

// File management helpers
std::optional<std::reference_wrapper<OpenFile>> LspServer::GetOpenFile(
    const std::string& uri) {
  auto it = open_files_.find(uri);
  if (it != open_files_.end()) {
    return std::ref(it->second);
  }
  return std::nullopt;
}

void LspServer::AddOpenFile(
    const std::string& uri, const std::string& content,
    const std::string& language_id, int version) {
  spdlog::debug("Adding open file: {}", uri);
  OpenFile file{uri, content, language_id, version};
  open_files_[uri] = std::move(file);
}

void LspServer::UpdateOpenFile(
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

void LspServer::RemoveOpenFile(const std::string& uri) {
  spdlog::debug("Removing open file: {}", uri);
  open_files_.erase(uri);
}

auto LspServer::PublishDiagnostics(const PublishDiagnosticsParams& params)
    -> asio::awaitable<void> {
  spdlog::debug("Publishing diagnostics for file: {}", params.uri);
  co_await endpoint_->SendNotification(
      "textDocument/publishDiagnostics", nlohmann::json(params));
}

}  // namespace lsp

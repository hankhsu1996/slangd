#include <memory>
#include <string>

#include <asio.hpp>
#include <jsonrpc/endpoint/endpoint.hpp>
#include <jsonrpc/transport/framed_pipe_transport.hpp>
#include <spdlog/spdlog.h>

#include "slangd/slangd_lsp_server.hpp"

using jsonrpc::endpoint::RpcEndpoint;
using jsonrpc::transport::FramedPipeTransport;
using slangd::SlangdLspServer;

int main(int argc, char* argv[]) {
  spdlog::set_pattern("[%l] %v");
  spdlog::set_level(spdlog::level::info);
  spdlog::flush_on(spdlog::level::info);

  // Parse command-line arguments
  const std::vector<std::string> args(argv, argv + argc);

  // Basic argument validation
  const std::string pipe_prefix = "--pipe=";
  if (args.size() < 2 || !args[1].starts_with(pipe_prefix)) {
    spdlog::error("Usage: <executable> --pipe=<pipe name>");
    return 1;
  }

  const std::string pipe_name = args[1].substr(pipe_prefix.length());
  spdlog::debug("Using pipe: {}", pipe_name);

  // Create the IO context
  asio::io_context io_context;
  auto executor = io_context.get_executor();

  // Create transport and endpoint
  auto transport =
      std::make_unique<FramedPipeTransport>(executor, pipe_name, false);

  auto endpoint = std::make_unique<RpcEndpoint>(executor, std::move(transport));

  // Create the LSP server
  auto server =
      std::make_unique<SlangdLspServer>(executor, std::move(endpoint));

  // Start the server asynchronously
  asio::co_spawn(
      io_context,
      [&server]() -> asio::awaitable<void> {
        auto result = co_await server->Start();
        if (result.has_value()) {
          spdlog::debug("Server run completed");
        } else {
          spdlog::error("Server error: {}", result.error().Message());
        }
      },
      asio::detached);

  // Run io_context
  io_context.run();

  spdlog::debug("Main thread completed");
  return 0;
}

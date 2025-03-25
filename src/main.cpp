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
  spdlog::set_level(spdlog::level::debug);

  // Parse command-line arguments
  const std::vector<std::string> args(argv, argv + argc);

  // Basic argument validation
  const std::string pipe_prefix = "--pipe=";
  if (args.size() < 2 || !args[1].starts_with(pipe_prefix)) {
    spdlog::error("Usage: <executable> --pipe=<pipe name>");
    return 1;
  }

  const std::string pipe_name = args[1].substr(pipe_prefix.length());
  spdlog::info("Using pipe: {}", pipe_name);

  // Create the IO context
  asio::io_context io_context;

  // Create transport and endpoint
  auto transport =
      std::make_unique<FramedPipeTransport>(io_context, pipe_name, false);

  auto endpoint =
      std::make_unique<RpcEndpoint>(io_context, std::move(transport));

  // Set up error handling
  endpoint->SetErrorHandler([](auto code, const auto& message) {
    spdlog::error(
        "JSON-RPC error: {} (code: {})", message, static_cast<int>(code));
  });

  // Create the server
  auto server =
      std::make_unique<SlangdLspServer>(io_context, std::move(endpoint));

  // Start the server asynchronously
  asio::co_spawn(
      io_context,
      [&server]() -> asio::awaitable<void> {
        try {
          // Run the server
          co_await server->Run();
          spdlog::info("Server run completed");
        } catch (const std::exception& ex) {
          // Log the error
          spdlog::error("Server error: {}", ex.what());
        }

        // Regardless of how we exit Run(), try to shut down gracefully
        // This is outside the try/catch block
        if (server) {
          try {
            co_await server->Shutdown();
          } catch (const std::exception& shutdown_ex) {
            spdlog::error("Shutdown error: {}", shutdown_ex.what());
          }
        }
      },
      asio::detached);

  // Run io_context
  io_context.run();

  return 0;
}

#include <iostream>
#include <memory>
#include <string>

#include <asio.hpp>
#include <jsonrpc/endpoint/endpoint.hpp>
#include <jsonrpc/transport/pipe_transport.hpp>

#include "slangd/slangd_lsp_server.hpp"

using jsonrpc::endpoint::RpcEndpoint;
using jsonrpc::transport::PipeTransport;
using slangd::SlangdLspServer;

int main(int argc, char* argv[]) {
  // Parse command-line arguments
  const std::vector<std::string> args(argv, argv + argc);

  // Basic argument validation
  const std::string pipe_prefix = "--pipe=";
  if (args.size() < 2 || !args[1].starts_with(pipe_prefix)) {
    std::cerr << "Usage: <executable> --pipe=<pipe name>" << std::endl;
    return 1;
  }

  const std::string pipe_name = args[1].substr(pipe_prefix.length());
  std::cout << "Using pipe: " << pipe_name << std::endl;

  // Create the IO context
  asio::io_context io_context;

  // Create transport and endpoint
  auto transport =
      std::make_unique<PipeTransport>(io_context, pipe_name, false);

  auto endpoint =
      std::make_unique<RpcEndpoint>(io_context, std::move(transport));

  // Set up error handling
  endpoint->SetErrorHandler([](auto code, const auto& message) {
    std::cerr << "JSON-RPC error: " << message
              << " (code: " << static_cast<int>(code) << ")" << std::endl;
  });

  // Create the server
  auto server =
      std::make_unique<SlangdLspServer>(io_context, std::move(endpoint));

  // Start the server asynchronously
  asio::co_spawn(
      io_context, [&server]() { return server->Run(); },
      [](std::exception_ptr e) {
        if (e) {
          try {
            std::rethrow_exception(e);
          } catch (const std::exception& ex) {
            std::cerr << "Server terminated with error: " << ex.what()
                      << std::endl;
          }
        } else {
          std::cout << "Server terminated gracefully" << std::endl;
        }
      });

  // Run io_context
  io_context.run();

  return 0;
}

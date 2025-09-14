#include <memory>
#include <string>
#include <vector>

#include <asio.hpp>
#include <jsonrpc/endpoint/endpoint.hpp>
#include <jsonrpc/transport/framed_pipe_transport.hpp>
#include <spdlog/spdlog.h>

#include "app/app_setup.hpp"
#include "app/crash_handler.hpp"
#include "slangd/core/slangd_lsp_server.hpp"
#include "slangd/services/legacy/legacy_language_service.hpp"

using jsonrpc::endpoint::RpcEndpoint;
using jsonrpc::transport::FramedPipeTransport;
using slangd::LegacyLanguageService;
using slangd::SlangdLspServer;

auto main(int argc, char* argv[]) -> int {
  // Initialize debugging features
  app::WaitForDebuggerIfRequested();
  app::InitializeCrashHandlers();

  // Parse command-line arguments
  const std::vector<std::string> args(argv, argv + argc);
  auto pipe_name_opt = app::ParsePipeName(args);
  if (!pipe_name_opt) {
    spdlog::error("Usage: <executable> --pipe=<pipe name>");
    return 1;
  }
  const std::string pipe_name = pipe_name_opt.value();

  // Setup loggers
  auto loggers = app::SetupLoggers();

  // Create the IO context
  asio::io_context io_context;
  auto executor = io_context.get_executor();

  // Create transport and endpoint
  auto transport = std::make_unique<FramedPipeTransport>(
      executor, pipe_name, false, loggers["transport"]);

  auto endpoint = std::make_unique<RpcEndpoint>(
      executor, std::move(transport), loggers["jsonrpc"]);

  // Create the language service and LSP server with dependency injection
  auto language_service =
      std::make_shared<LegacyLanguageService>(executor, loggers["slangd"]);
  auto server = std::make_unique<SlangdLspServer>(
      executor, std::move(endpoint), language_service, loggers["slangd"]);

  // Start the server asynchronously
  asio::co_spawn(
      io_context,
      [&server]() -> asio::awaitable<void> {
        auto result = co_await server->Start();
        if (!result.has_value()) {
          spdlog::error("Server error: {}", result.error().Message());
        }
        co_return;
      },
      asio::detached);

  io_context.run();
  return 0;
}

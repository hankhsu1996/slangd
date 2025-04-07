#include <memory>
#include <string>

#include <asio.hpp>
#include <jsonrpc/endpoint/endpoint.hpp>
#include <jsonrpc/transport/framed_pipe_transport.hpp>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include "slangd/core/slangd_lsp_server.hpp"

using jsonrpc::endpoint::RpcEndpoint;
using jsonrpc::transport::FramedPipeTransport;
using slangd::SlangdLspServer;

auto ParsePipeName(const std::vector<std::string>& args)
    -> std::optional<std::string> {
  const std::string pipe_prefix = "--pipe=";
  if (args.size() < 2 || !args[1].starts_with(pipe_prefix)) {
    return std::nullopt;
  }
  return args[1].substr(pipe_prefix.length());
}

auto SetupLoggers()
    -> std::unordered_map<std::string, std::shared_ptr<spdlog::logger>> {
  // Configure default logger - disable it to avoid unexpected output
  spdlog::set_level(spdlog::level::off);

  // Create named loggers
  auto transport_logger = spdlog::stdout_color_mt("transport");
  auto jsonrpc_logger = spdlog::stdout_color_mt("jsonrpc");
  auto slangd_logger = spdlog::stdout_color_mt("slangd");

  // Configure pattern and flush behavior centrally for all loggers
  const auto logger_pairs =
      std::vector<std::pair<std::string, std::shared_ptr<spdlog::logger>>>{
          {"transport", transport_logger},
          {"jsonrpc", jsonrpc_logger},
          {"slangd", slangd_logger},
      };

  for (const auto& [name, logger] : logger_pairs) {
    logger->set_pattern("[%7n][%5l] %v");
    logger->flush_on(spdlog::level::debug);
  }

  // Configure individual logger levels
  transport_logger->set_level(spdlog::level::off);
  jsonrpc_logger->set_level(spdlog::level::debug);
  slangd_logger->set_level(spdlog::level::debug);

  return {
      {"transport", transport_logger},
      {"jsonrpc", jsonrpc_logger},
      {"slangd", slangd_logger},
  };
}

int main(int argc, char* argv[]) {
  // Parse command-line arguments
  const std::vector<std::string> args(argv, argv + argc);
  auto pipe_name_opt = ParsePipeName(args);
  if (!pipe_name_opt) {
    spdlog::error("Usage: <executable> --pipe=<pipe name>");
    return 1;
  }
  const std::string pipe_name = pipe_name_opt.value();

  // Setup loggers
  auto loggers = SetupLoggers();

  // Create the IO context
  asio::io_context io_context;
  auto executor = io_context.get_executor();

  // Create transport and endpoint
  auto transport = std::make_unique<FramedPipeTransport>(
      executor, pipe_name, false, loggers["transport"]);

  auto endpoint = std::make_unique<RpcEndpoint>(
      executor, std::move(transport), loggers["jsonrpc"]);

  // Create the LSP server
  auto server = std::make_unique<SlangdLspServer>(
      executor, std::move(endpoint), loggers["slangd"]);

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

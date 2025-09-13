#include "app/app_setup.hpp"

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace app {

// Global var to enable debug print
constexpr bool kDebugPrint = true;

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
  if (kDebugPrint) {
    spdlog::set_level(spdlog::level::debug);
  } else {
    spdlog::set_level(spdlog::level::off);
  }

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
  if (kDebugPrint) {
    transport_logger->set_level(spdlog::level::info);
    jsonrpc_logger->set_level(spdlog::level::info);
    slangd_logger->set_level(spdlog::level::debug);
  } else {
    transport_logger->set_level(spdlog::level::off);
    jsonrpc_logger->set_level(spdlog::level::off);
    slangd_logger->set_level(spdlog::level::info);
  }

  return {
      {"transport", transport_logger},
      {"jsonrpc", jsonrpc_logger},
      {"slangd", slangd_logger},
  };
}

}  // namespace app
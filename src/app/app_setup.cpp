#include "app/app_setup.hpp"

#include <array>
#include <cstdlib>
#include <string_view>
#include <unordered_map>

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace app {

namespace {

constexpr std::string_view kDefaultLogLevel = "debug";
constexpr std::string_view kLogPattern = "[%n][%L] %v";

struct LoggerConfig {
  std::string_view name;
  spdlog::level::level_enum level;
};

auto ParseLogLevel(std::string_view level_str) -> spdlog::level::level_enum {
  static const std::unordered_map<std::string_view, spdlog::level::level_enum>
      kLevelMap = {
          {"trace", spdlog::level::trace}, {"debug", spdlog::level::debug},
          {"info", spdlog::level::info},   {"warn", spdlog::level::warn},
          {"error", spdlog::level::err},   {"off", spdlog::level::off},
      };

  if (auto it = kLevelMap.find(level_str); it != kLevelMap.end()) {
    return it->second;
  }
  return spdlog::level::debug;
}

auto GetLogLevelFromEnv() -> spdlog::level::level_enum {
  const char* env_level = std::getenv("SPDLOG_LEVEL");
  return ParseLogLevel(env_level != nullptr ? env_level : kDefaultLogLevel);
}

void ConfigureLogger(
    std::shared_ptr<spdlog::logger> logger, spdlog::level::level_enum level) {
  logger->set_pattern(std::string(kLogPattern));
  logger->set_level(level);
  logger->flush_on(spdlog::level::debug);
}

}  // namespace

auto ParsePipeName(const std::vector<std::string>& args)
    -> std::optional<std::string> {
  constexpr std::string_view kPipePrefix = "--pipe=";
  if (args.size() < 2 || !args[1].starts_with(kPipePrefix)) {
    return std::nullopt;
  }
  return args[1].substr(kPipePrefix.length());
}

auto SetupLoggers()
    -> std::unordered_map<std::string, std::shared_ptr<spdlog::logger>> {
  const auto user_log_level = GetLogLevelFromEnv();
  spdlog::set_level(user_log_level);

  constexpr std::array kLoggerConfigs = {
      LoggerConfig{.name = "transport", .level = spdlog::level::info},
      LoggerConfig{.name = "jsonrpc", .level = spdlog::level::info},
      LoggerConfig{.name = "slangd", .level = spdlog::level::trace},
  };

  std::unordered_map<std::string, std::shared_ptr<spdlog::logger>> loggers;

  for (const auto& config : kLoggerConfigs) {
    auto logger = spdlog::stdout_color_mt(std::string(config.name));
    const auto level =
        (config.name == "slangd") ? user_log_level : config.level;
    ConfigureLogger(logger, level);
    loggers[std::string(config.name)] = std::move(logger);
  }

  return loggers;
}

}  // namespace app

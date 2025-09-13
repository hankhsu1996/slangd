#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <spdlog/logger.h>

namespace app {

/// Parse command line arguments to extract pipe name
/// Returns the pipe name if --pipe=<name> is found, nullopt otherwise
auto ParsePipeName(const std::vector<std::string>& args)
    -> std::optional<std::string>;

/// Setup structured logging with named loggers
/// Returns configured loggers for transport, jsonrpc, and slangd components
auto SetupLoggers()
    -> std::unordered_map<std::string, std::shared_ptr<spdlog::logger>>;

}  // namespace app

#include <array>
#include <csignal>
#include <cxxabi.h>
#include <dlfcn.h>
#include <execinfo.h>
#include <memory>
#include <string>
#include <string_view>
#include <unistd.h>

#include <asio.hpp>
#include <jsonrpc/endpoint/endpoint.hpp>
#include <jsonrpc/transport/framed_pipe_transport.hpp>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include "slangd/core/slangd_lsp_server.hpp"

using jsonrpc::endpoint::RpcEndpoint;
using jsonrpc::transport::FramedPipeTransport;
using slangd::SlangdLspServer;
using std::free;
// global var to enable debug print
constexpr bool kDebugPrint = true;
constexpr bool kWaitForGdb = false;

static void WaitForDebuggerIfRequested() {
  if (std::getenv("WAIT_FOR_GDB") != nullptr) {
    std::raise(SIGSTOP);
  }
}

static void PrintFuncNames(int skip = 1, int max_frames = 32) noexcept {
  std::array<void*, 128> frames{};
  const int n = ::backtrace(frames.data(), static_cast<int>(frames.size()));
  if (n <= 0) {
    return;
  }

  constexpr std::string_view kHdr = "Fatal signal received\n";
  (void)(::write(STDERR_FILENO, kHdr.data(), kHdr.size()) == 0);

  const int begin = std::clamp(skip, 0, n);
  const int count = std::max(0, std::min(max_frames, n - begin));
  auto used =
      std::span<void*>{frames}
          .first(static_cast<size_t>(n))
          .subspan(static_cast<size_t>(begin), static_cast<size_t>(count));

  size_t idx = 0;
  for (void* addr : used) {
    Dl_info info{};
    if (::dladdr(addr, &info) != 0 && info.dli_sname != nullptr) {
      // Try to demangle into a preallocated buffer to avoid heap allocs.
      std::array<char, 1024> dem_buf{};
      size_t dem_size = dem_buf.size();
      int status = 0;
      char* raw = abi::__cxa_demangle(
          info.dli_sname, dem_buf.data(), &dem_size, &status);
      // Own only if __cxa_demangle allocated a new buffer.
      std::unique_ptr<char, void (*)(void*)> guard(
          ((raw != nullptr) && raw != dem_buf.data()) ? raw : nullptr,
          std::free);
      const char* name =
          (status == 0 && (raw != nullptr)) ? raw : info.dli_sname;

      std::array<char, 512> line{};
      int written =
          ::snprintf(line.data(), line.size(), "  [%zu] %s\n", idx, name);
      if (written > 0) {
        const size_t len =
            static_cast<size_t>(std::min<int>(written, line.size()));
        (void)(::write(STDERR_FILENO, line.data(), len) == 0);
      }
    } else {
      std::array<char, 64> line{};
      int written = ::snprintf(line.data(), line.size(), "  [%zu] ??\n", idx);
      if (written > 0) {
        const size_t len =
            static_cast<size_t>(std::min<int>(written, line.size()));
        (void)(::write(STDERR_FILENO, line.data(), len) == 0);
      }
    }
    ++idx;
  }
}

static void HandleSegfault(int sig) noexcept {
  std::signal(sig, SIG_DFL);
  PrintFuncNames(1, 24);
  ::raise(sig);
}

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

auto main(int argc, char* argv[]) -> int {
  if (kWaitForGdb) {
    WaitForDebuggerIfRequested();
  }

  // Install crash handlers for various fatal signals
  std::signal(SIGSEGV, HandleSegfault);  // Segmentation fault
  std::signal(SIGFPE, HandleSegfault);   // Floating point exception
  std::signal(SIGILL, HandleSegfault);   // Illegal instruction
  std::signal(SIGBUS, HandleSegfault);   // Bus error

  // Debug logging enabled
  if (kDebugPrint) {
    spdlog::info("slangd PID: {} - debug logging enabled", getpid());
  }

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

#include "slangd/utils/scoped_timer.hpp"

#include <fmt/format.h>

namespace slangd::utils {

ScopedTimer::ScopedTimer(
    std::string operation_name, std::shared_ptr<spdlog::logger> logger)
    : start_(std::chrono::steady_clock::now()),
      operation_name_(std::move(operation_name)),
      logger_(logger ? logger : spdlog::default_logger()) {
}

ScopedTimer::~ScopedTimer() {
  auto elapsed = GetElapsed();
  logger_->debug("{} completed ({})", operation_name_, FormatDuration(elapsed));
}

auto ScopedTimer::GetElapsed() const -> std::chrono::milliseconds {
  auto end_time = std::chrono::steady_clock::now();
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      end_time - start_);
}

auto ScopedTimer::FormatDuration(std::chrono::milliseconds duration)
    -> std::string {
  auto count = duration.count();

  if (count >= 1000) {
    // Format as seconds with one decimal place
    auto seconds = static_cast<double>(count) / 1000.0;
    return fmt::format("{:.1f}s", seconds);
  }
  // Format as milliseconds
  return fmt::format("{}ms", count);
}

}  // namespace slangd::utils

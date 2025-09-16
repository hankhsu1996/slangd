#pragma once

#include <chrono>
#include <memory>
#include <string>

#include <spdlog/spdlog.h>

namespace slangd::utils {

class ScopedTimer {
 public:
  ScopedTimer(const ScopedTimer &) = default;
  ScopedTimer(ScopedTimer &&) = delete;
  auto operator=(const ScopedTimer &) -> ScopedTimer & = default;
  auto operator=(ScopedTimer &&) -> ScopedTimer & = delete;
  ScopedTimer(
      std::string operation_name, std::shared_ptr<spdlog::logger> logger);
  ~ScopedTimer();

  // Get elapsed time without destroying the timer
  [[nodiscard]] auto GetElapsed() const -> std::chrono::milliseconds;

  // Format duration as "123ms" or "1.2s" for readability
  static auto FormatDuration(std::chrono::milliseconds duration) -> std::string;

 private:
  std::chrono::steady_clock::time_point start_;
  std::string operation_name_;
  std::shared_ptr<spdlog::logger> logger_;
};

}  // namespace slangd::utils

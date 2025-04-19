#pragma once

#include <chrono>
#include <string>
#include <utility>

#include <spdlog/spdlog.h>

namespace slangd {

// Simple timer utility class for performance measurements
class ScopedTimer {
 public:
  ScopedTimer(const ScopedTimer &) = default;
  ScopedTimer(ScopedTimer &&) = delete;
  auto operator=(const ScopedTimer &) -> ScopedTimer & = default;
  auto operator=(ScopedTimer &&) -> ScopedTimer & = delete;

  explicit ScopedTimer(
      std::string operation_name,
      std::shared_ptr<spdlog::logger> logger = nullptr)
      : logger_(logger ? logger : spdlog::default_logger()),
        operation_name_(std::move(operation_name)),
        start_time_(std::chrono::high_resolution_clock::now()) {
  }

  ~ScopedTimer() {
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                        end_time - start_time_)
                        .count();
    logger_->debug(
        "ScopedTimer {} completed in {} ms", operation_name_, duration);
  }

 private:
  std::shared_ptr<spdlog::logger> logger_;
  std::string operation_name_;
  std::chrono::time_point<std::chrono::high_resolution_clock> start_time_;
};

}  // namespace slangd

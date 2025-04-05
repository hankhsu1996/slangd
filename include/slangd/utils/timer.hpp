#pragma once

#include <chrono>
#include <string>

#include <spdlog/spdlog.h>

namespace slangd {

// Simple timer utility class for performance measurements
class ScopedTimer {
 public:
  ScopedTimer(const std::string& operation_name)
      : operation_name_(operation_name),
        start_time_(std::chrono::high_resolution_clock::now()) {}

  ~ScopedTimer() {
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                        end_time - start_time_)
                        .count();
    spdlog::info(
        "ScopedTimer {} completed in {} ms", operation_name_, duration);
  }

 private:
  std::string operation_name_;
  std::chrono::time_point<std::chrono::high_resolution_clock> start_time_;
};

}  // namespace slangd

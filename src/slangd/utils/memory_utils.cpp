#include "slangd/utils/memory_utils.hpp"

#include <fstream>
#include <sstream>
#include <string>

namespace slangd::utils {

auto GetRssMB() -> size_t {
  std::ifstream status("/proc/self/status");
  std::string line;
  while (std::getline(status, line)) {
    if (line.starts_with("VmRSS:")) {
      // Format: "VmRSS:     1062508 kB"
      std::istringstream iss(line.substr(6));  // Skip "VmRSS:"
      size_t kb = 0;
      if (iss >> kb) {
        return kb / 1024;  // Convert to MB
      }
    }
  }
  return 0;
}

}  // namespace slangd::utils

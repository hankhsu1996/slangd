#pragma once

#include <cstddef>

namespace slangd::utils {

// Get current RSS (Resident Set Size) in MB from /proc/self/status
// Returns 0 if unable to read or parse the value
auto GetRssMB() -> size_t;

}  // namespace slangd::utils

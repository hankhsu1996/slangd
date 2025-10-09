#pragma once

#include <string>

namespace slangd {

// Document state tracked by the language service
struct DocumentState {
  std::string content;
  int version;
};

}  // namespace slangd

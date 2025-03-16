#include <iostream>
#include <memory>

#include "slangd/slangd_lsp_server.hpp"

int main() {
  try {
    std::cout << "Starting SystemVerilog Language Server..." << std::endl;

    // Create and run the server
    auto server = std::make_unique<slangd::SlangdLspServer>();
    server->Run();

    std::cout << "Server exited normally" << std::endl;
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Fatal error: " << e.what() << std::endl;
    return 1;
  }
}

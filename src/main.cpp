#include <iostream>
#include <memory>

#include <asio.hpp>

#include "slangd/slangd_lsp_server.hpp"

int main() {
  try {
    // Create the IO context
    asio::io_context io_context;

    // Create and run the server
    auto server = std::make_unique<slangd::SlangdLspServer>(io_context);
    server->Run();

    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Fatal error: " << e.what() << std::endl;
    return 1;
  }
}

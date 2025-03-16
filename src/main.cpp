#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#include <asio.hpp>

#include "slangd/slangd_lsp_server.hpp"

int main() {
  try {
    // Create the IO context
    asio::io_context io_context;

    // Create and start the server
    auto server = std::make_unique<slangd::SlangdLspServer>(io_context);
    server->Run();

    // Determine number of threads to use (use hardware concurrency)
    const unsigned int thread_count = std::thread::hardware_concurrency();
    const unsigned int num_threads = thread_count > 0 ? thread_count - 1 : 1;

    std::cout << "Running io_context with " << num_threads
              << " additional threads" << std::endl;

    // Create a thread pool to run the io_context
    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    // Start worker threads (main thread will also run io_context)
    for (unsigned int i = 0; i < num_threads; ++i) {
      threads.emplace_back([&io_context]() { io_context.run(); });
    }

    // Run io_context in the main thread too
    io_context.run();

    // Wait for all threads to complete
    for (auto& thread : threads) {
      thread.join();
    }

    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Fatal error: " << e.what() << std::endl;
    return 1;
  }
}

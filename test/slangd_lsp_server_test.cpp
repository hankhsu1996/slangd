#include "slangd/slangd_lsp_server.hpp"

#include <iostream>
#include <string>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/signal_set.hpp>
#include <asio/use_awaitable.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

using namespace Catch::Matchers;

namespace slangd {

namespace {

// Simple test file content representing a SystemVerilog module
const std::string kTestFileContent = R"(
module test_module(
  input  logic clk,
  input  logic rst_n,
  input  logic [7:0] data_in,
  output logic [7:0] data_out
);
  // Simple test logic
  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      data_out <= 8'h00;
    end else begin
      data_out <= data_in;
    end
  end
endmodule
)";

}  // namespace

// Forward declare SlangdLspServer to help with friend declaration
class SlangdLspServer;

// Test fixture to access private members of SlangdLspServer
class TestSlangdLspServer {
 public:
  // Static test methods to access private members of SlangdLspServer
  static asio::awaitable<void> TestIndexFile(
      SlangdLspServer& server, const std::string& uri,
      const std::string& content);

  static asio::awaitable<std::vector<Symbol>> TestFindSymbols(
      SlangdLspServer& server, const std::string& query);

  static asio::awaitable<nlohmann::json> TestHandleWorkspaceSymbol(
      SlangdLspServer& server, const std::optional<nlohmann::json>& params);
};

// Implementation of the test methods
asio::awaitable<void> TestSlangdLspServer::TestIndexFile(
    SlangdLspServer& server, const std::string& uri,
    const std::string& content) {
  return server.IndexFile(uri, content);
}

asio::awaitable<std::vector<Symbol>> TestSlangdLspServer::TestFindSymbols(
    SlangdLspServer& server, const std::string& query) {
  return server.FindSymbols(query);
}

asio::awaitable<nlohmann::json> TestSlangdLspServer::TestHandleWorkspaceSymbol(
    SlangdLspServer& server, const std::optional<nlohmann::json>& params) {
  return server.HandleWorkspaceSymbol(params);
}

TEST_CASE("SlangdLspServer symbol extraction", "[lsp][slangd]") {
  // Set up a server with io_context for asynchronous operation
  asio::io_context io_context;
  SlangdLspServer server(io_context);

  // Run the test asynchronously
  bool test_complete = false;

  asio::co_spawn(
      io_context,
      [&]() -> asio::awaitable<void> {
        // Index a test .sv file
        const std::string uri = "file:///test.sv";
        co_await TestSlangdLspServer::TestIndexFile(
            server, uri, kTestFileContent);

        // Print a debug statement to verify indexing
        std::cout << "Test file indexed, searching for symbols..." << std::endl;

        // Search for symbols
        auto symbols =
            co_await TestSlangdLspServer::TestFindSymbols(server, "test");
        REQUIRE(!symbols.empty());
        REQUIRE(symbols.size() == 1);
        REQUIRE(symbols[0].name == "test_module");
        REQUIRE(symbols[0].type == SymbolType::Module);

        // Test workspace/symbol request handling
        nlohmann::json params = {{"query", "test"}};
        auto result = co_await TestSlangdLspServer::TestHandleWorkspaceSymbol(
            server, params);

        REQUIRE(!result.empty());
        REQUIRE(result.size() == 1);
        REQUIRE(result[0]["name"] == "test_module");

        test_complete = true;
      },
      [](std::exception_ptr e) {
        if (e) {
          try {
            std::rethrow_exception(e);
          } catch (const std::exception& e) {
            std::cerr << "Test failed with exception: " << e.what()
                      << std::endl;
            FAIL(e.what());
          }
        }
      });

  // Run the io_context with a timeout
  asio::signal_set signals(io_context, SIGINT, SIGTERM);
  signals.async_wait([&](auto, auto) { io_context.stop(); });

  // Set a timeout
  auto timer = std::make_shared<asio::steady_timer>(io_context);
  timer->expires_after(std::chrono::seconds(3));
  timer->async_wait([&](std::error_code ec) {
    if (!ec) {
      std::cout << "Test timeout reached, stopping io_context" << std::endl;
      io_context.stop();
    }
  });

  // Run until completion or timeout
  io_context.run();

  // Ensure test completed successfully
  REQUIRE(test_complete);
}

}  // namespace slangd

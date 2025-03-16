#include <filesystem>
#include <fstream>
#include <string>

#include <asio.hpp>
#include <catch2/catch_test_macros.hpp>

#include "slangd/document_manager.hpp"

// Helper function to read a file into a string
std::string ReadFile(const std::string& path) {
  std::ifstream file(path);
  if (!file.is_open()) {
    throw std::runtime_error("Could not open file: " + path);
  }
  return std::string(
      std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

TEST_CASE("DocumentManager initialization", "[basic]") {
  asio::io_context io_context;
  REQUIRE_NOTHROW(slangd::DocumentManager(io_context));
  INFO("DocumentManager can be initialized");
}

// Test that we can read test files
TEST_CASE("DocumentManager can read files", "[basic]") {
  // Set up paths
  std::filesystem::path current_dir = std::filesystem::current_path();
  std::string file_path =
      (current_dir / "test" / "testfiles" / "simple_module.sv").string();

  // Read the file content
  REQUIRE_NOTHROW(ReadFile(file_path));
  std::string content = ReadFile(file_path);
  REQUIRE(content.length() > 0);
  INFO("Read file content with length: " << content.length());
}

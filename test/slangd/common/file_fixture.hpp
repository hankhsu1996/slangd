#pragma once

#include <filesystem>
#include <fstream>
#include <string_view>

#include "slangd/utils/canonical_path.hpp"

namespace slangd::test {

// Base fixture for tests that need temporary file management
class FileTestFixture {
 public:
  FileTestFixture() : FileTestFixture("slangd_test") {
  }

  explicit FileTestFixture(std::string_view prefix) {
    // Create a temporary directory for test files
    temp_dir_ = std::filesystem::temp_directory_path() / prefix;
    std::filesystem::create_directories(temp_dir_);
  }

  ~FileTestFixture() {
    // Clean up test files
    std::error_code ec;
    std::filesystem::remove_all(temp_dir_, ec);
  }

  // Explicitly delete copy operations
  FileTestFixture(const FileTestFixture&) = delete;
  auto operator=(const FileTestFixture&) -> FileTestFixture& = delete;

  // Explicitly delete move operations
  FileTestFixture(FileTestFixture&&) = delete;
  auto operator=(FileTestFixture&&) -> FileTestFixture& = delete;

  [[nodiscard]] auto GetTempDir() const -> slangd::CanonicalPath {
    return slangd::CanonicalPath(temp_dir_);
  }

  auto CreateFile(std::string_view filename, std::string_view content)
      -> slangd::CanonicalPath {
    auto file_path = temp_dir_ / filename;
    std::ofstream file(file_path);
    file << content;
    file.close();
    return slangd::CanonicalPath(file_path);
  }

 private:
  std::filesystem::path temp_dir_;
};

}  // namespace slangd::test

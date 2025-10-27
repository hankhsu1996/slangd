#pragma once

#include <cstdlib>
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
    // Use TEST_TMPDIR as required by Bazel test specification
    // Falls back to system temp directory for non-Bazel environments
    std::filesystem::path base_temp;
    if (const char* test_tmpdir = std::getenv("TEST_TMPDIR")) {
      base_temp = test_tmpdir;
    } else {
      base_temp = std::filesystem::temp_directory_path();
    }

    temp_dir_ = base_temp / prefix;
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

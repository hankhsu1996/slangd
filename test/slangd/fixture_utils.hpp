#include <cstdlib>
#include <fstream>
#include <string>

#include "tools/cpp/runfiles/runfiles.h"

using bazel::tools::cpp::runfiles::Runfiles;

// Helper to get path to test files using Bazel runfiles
inline std::string GetTestFilePath(const std::string& filename) {
  std::string error;
  std::unique_ptr<Runfiles> runfiles(Runfiles::CreateForTest(&error));
  if (!runfiles) {
    throw std::runtime_error("Failed to create runfiles object: " + error);
  }

  std::string path =
      runfiles->Rlocation("_main/test/slangd/fixtures/" + filename);

  if (path.empty()) {
    throw std::runtime_error(
        "Could not find test file in runfiles: " + filename);
  }

  return path;
}

// Helper function to read a file into a string
inline std::string ReadFile(const std::string& path) {
  std::ifstream file(path);
  if (!file.is_open()) {
    throw std::runtime_error("Could not open file: " + path);
  }
  return std::string(
      std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

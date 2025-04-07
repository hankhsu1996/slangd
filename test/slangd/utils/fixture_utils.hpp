#include <cstdlib>
#include <fstream>
#include <string>

#include "tools/cpp/runfiles/runfiles.h"

using bazel::tools::cpp::runfiles::Runfiles;

extern std::string g_runfile_path;

// Helper to get path to test files using Bazel runfiles
inline std::string GetTestFilePath(const std::string& filename) {
  std::string path = g_runfile_path + "/" + filename;

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

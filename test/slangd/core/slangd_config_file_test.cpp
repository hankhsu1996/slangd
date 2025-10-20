#include "slangd/core/slangd_config_file.hpp"

#include <filesystem>
#include <fstream>

#include <catch2/catch_all.hpp>
#include <spdlog/spdlog.h>

#include "slangd/utils/canonical_path.hpp"

constexpr auto kLogLevel = spdlog::level::debug;

auto main(int argc, char* argv[]) -> int {
  spdlog::set_level(kLogLevel);
  spdlog::set_pattern("[%l] %v");

  // Suppress Bazel test sharding warnings
  setenv("TEST_SHARD_INDEX", "0", 0);
  setenv("TEST_TOTAL_SHARDS", "1", 0);
  setenv("TEST_SHARD_STATUS_FILE", "", 0);

  return Catch::Session().run(argc, argv);
}

// Helper to create temporary .slangd config file
class TempConfigFile {
 public:
  explicit TempConfigFile(std::string_view content) {
    path_ = std::filesystem::temp_directory_path() / ".slangd_test";
    std::ofstream file(path_);
    file << content;
  }

  ~TempConfigFile() {
    if (std::filesystem::exists(path_)) {
      std::filesystem::remove(path_);
    }
  }

  TempConfigFile(const TempConfigFile&) = delete;
  auto operator=(const TempConfigFile&) -> TempConfigFile& = delete;
  TempConfigFile(TempConfigFile&&) = delete;
  auto operator=(TempConfigFile&&) -> TempConfigFile& = delete;

  [[nodiscard]] auto Path() const -> slangd::CanonicalPath {
    return slangd::CanonicalPath(path_);
  }

 private:
  std::filesystem::path path_;
};

TEST_CASE("SlangdConfigFile PathExclude filters matching paths", "[config]") {
  const auto* content = R"(
If:
  PathExclude: .*/generated/.*
)";

  TempConfigFile temp(content);
  auto config = slangd::SlangdConfigFile::LoadFromFile(temp.Path());

  REQUIRE(config.has_value());

  // Should exclude paths matching the pattern
  REQUIRE(!config->ShouldIncludeFile("rtl/generated/generated.sv"));
  REQUIRE(!config->ShouldIncludeFile("tb/generated/wrapper.sv"));

  // Should include paths not matching the pattern
  REQUIRE(config->ShouldIncludeFile("rtl/design.sv"));
  REQUIRE(config->ShouldIncludeFile("tb/testbench.sv"));
}

TEST_CASE(
    "SlangdConfigFile PathMatch includes only matching paths", "[config]") {
  const auto* content = R"(
If:
  PathMatch: rtl/.*\.sv
)";

  TempConfigFile temp(content);
  auto config = slangd::SlangdConfigFile::LoadFromFile(temp.Path());

  REQUIRE(config.has_value());

  // Should include paths matching the pattern
  REQUIRE(config->ShouldIncludeFile("rtl/design.sv"));
  REQUIRE(config->ShouldIncludeFile("rtl/subdir/module.sv"));

  // Should exclude paths not matching the pattern
  REQUIRE(!config->ShouldIncludeFile("tb/testbench.sv"));
  REQUIRE(!config->ShouldIncludeFile("rtl/design.svh"));
}

TEST_CASE(
    "SlangdConfigFile PathMatch and PathExclude work together", "[config]") {
  const auto* content = R"(
If:
  PathMatch: rtl/.*
  PathExclude: .*/generated/.*
)";

  TempConfigFile temp(content);
  auto config = slangd::SlangdConfigFile::LoadFromFile(temp.Path());

  REQUIRE(config.has_value());

  // Should include: matches PathMatch AND doesn't match PathExclude
  REQUIRE(config->ShouldIncludeFile("rtl/design.sv"));

  // Should exclude: doesn't match PathMatch
  REQUIRE(!config->ShouldIncludeFile("tb/testbench.sv"));

  // Should exclude: matches PathMatch but also matches PathExclude
  REQUIRE(!config->ShouldIncludeFile("rtl/generated/generated.sv"));
}

TEST_CASE("SlangdConfigFile with no If block includes everything", "[config]") {
  const auto* content = R"(
Files:
  - test.sv
)";

  TempConfigFile temp(content);
  auto config = slangd::SlangdConfigFile::LoadFromFile(temp.Path());

  REQUIRE(config.has_value());

  // Should include all paths when no conditions specified
  REQUIRE(config->ShouldIncludeFile("rtl/design.sv"));
  REQUIRE(config->ShouldIncludeFile("rtl/generated/generated.sv"));
  REQUIRE(config->ShouldIncludeFile("tb/testbench.sv"));
}

TEST_CASE(
    "SlangdConfigFile with empty patterns includes everything", "[config]") {
  const auto* content = R"(
If:
)";

  TempConfigFile temp(content);
  auto config = slangd::SlangdConfigFile::LoadFromFile(temp.Path());

  REQUIRE(config.has_value());

  // Should include all paths when conditions are empty
  REQUIRE(config->ShouldIncludeFile("rtl/design.sv"));
  REQUIRE(config->ShouldIncludeFile("rtl/generated/generated.sv"));
}

TEST_CASE(
    "SlangdConfigFile with invalid regex includes by default", "[config]") {
  const auto* content = R"(
If:
  PathExclude: "[invalid"
)";

  TempConfigFile temp(content);
  auto config = slangd::SlangdConfigFile::LoadFromFile(temp.Path());

  REQUIRE(config.has_value());

  // Should include all paths when regex is invalid (fail open)
  REQUIRE(config->ShouldIncludeFile("rtl/design.sv"));
  REQUIRE(config->ShouldIncludeFile("rtl/generated/generated.sv"));
}

TEST_CASE(
    "SlangdConfigFile normalizes Windows paths to forward slashes",
    "[config]") {
  const auto* content = R"(
If:
  PathExclude: .*/generated/.*
)";

  TempConfigFile temp(content);
  auto config = slangd::SlangdConfigFile::LoadFromFile(temp.Path());

  REQUIRE(config.has_value());

  // Forward slashes should work (already normalized)
  REQUIRE(!config->ShouldIncludeFile("rtl/generated/generated.sv"));

  // The actual path normalization happens in ProjectLayoutBuilder
  // This test documents the expected input format
}

TEST_CASE(
    "SlangdConfigFile AutoDiscover defaults to true when omitted", "[config]") {
  const auto* content = R"(
Files:
  - test.sv
)";

  TempConfigFile temp(content);
  auto config = slangd::SlangdConfigFile::LoadFromFile(temp.Path());

  REQUIRE(config.has_value());
  REQUIRE(config->GetAutoDiscover() == true);
}

TEST_CASE(
    "SlangdConfigFile AutoDiscover true enables workspace discovery",
    "[config]") {
  const auto* content = R"(
AutoDiscover: true
Files:
  - external/uvm_pkg.sv
)";

  TempConfigFile temp(content);
  auto config = slangd::SlangdConfigFile::LoadFromFile(temp.Path());

  REQUIRE(config.has_value());
  REQUIRE(config->GetAutoDiscover() == true);
}

TEST_CASE(
    "SlangdConfigFile AutoDiscover false disables workspace discovery",
    "[config]") {
  const auto* content = R"(
AutoDiscover: false
Files:
  - rtl/design.sv
)";

  TempConfigFile temp(content);
  auto config = slangd::SlangdConfigFile::LoadFromFile(temp.Path());

  REQUIRE(config.has_value());
  REQUIRE(config->GetAutoDiscover() == false);
}

TEST_CASE(
    "SlangdConfigFile AutoDiscover false with FileLists uses only FileLists",
    "[config]") {
  const auto* content = R"(
AutoDiscover: false
FileLists:
  Paths:
    - rtl/rtl.f
)";

  TempConfigFile temp(content);
  auto config = slangd::SlangdConfigFile::LoadFromFile(temp.Path());

  REQUIRE(config.has_value());
  REQUIRE(config->GetAutoDiscover() == false);
}

TEST_CASE("SlangdConfigFile PathMatch with list uses OR logic", "[config]") {
  const auto* content = R"(
If:
  PathMatch:
    - rtl/.*\.sv
    - tb/.*\.sv
)";

  TempConfigFile temp(content);
  auto config = slangd::SlangdConfigFile::LoadFromFile(temp.Path());

  REQUIRE(config.has_value());

  // Should include files matching either pattern
  REQUIRE(config->ShouldIncludeFile("rtl/design.sv"));
  REQUIRE(config->ShouldIncludeFile("tb/testbench.sv"));

  // Should exclude files not matching any pattern
  REQUIRE(!config->ShouldIncludeFile("common/defines.sv"));
  REQUIRE(!config->ShouldIncludeFile("rtl/design.svh"));
}

TEST_CASE("SlangdConfigFile PathExclude with list uses OR logic", "[config]") {
  const auto* content = R"(
If:
  PathExclude:
    - .*/generated/.*
    - .*_tb\.sv
    - .*/build/.*
)";

  TempConfigFile temp(content);
  auto config = slangd::SlangdConfigFile::LoadFromFile(temp.Path());

  REQUIRE(config.has_value());

  // Should exclude files matching any pattern
  REQUIRE(!config->ShouldIncludeFile("rtl/generated/generated.sv"));
  REQUIRE(!config->ShouldIncludeFile("rtl/module_tb.sv"));
  REQUIRE(!config->ShouldIncludeFile("rtl/build/output.sv"));

  // Should include files not matching any pattern
  REQUIRE(config->ShouldIncludeFile("rtl/design.sv"));
  REQUIRE(config->ShouldIncludeFile("tb/testbench.sv"));
}

TEST_CASE("SlangdConfigFile PathMatch list AND PathExclude list", "[config]") {
  const auto* content = R"(
If:
  PathMatch:
    - rtl/.*
    - tb/.*
  PathExclude:
    - .*/generated/.*
    - .*_tb\.sv
)";

  TempConfigFile temp(content);
  auto config = slangd::SlangdConfigFile::LoadFromFile(temp.Path());

  REQUIRE(config.has_value());

  // Should include: matches PathMatch AND doesn't match PathExclude
  REQUIRE(config->ShouldIncludeFile("rtl/design.sv"));
  REQUIRE(config->ShouldIncludeFile("tb/testbench.sv"));

  // Should exclude: doesn't match any PathMatch pattern
  REQUIRE(!config->ShouldIncludeFile("common/utils.sv"));

  // Should exclude: matches PathMatch but also matches PathExclude
  REQUIRE(!config->ShouldIncludeFile("rtl/generated/gen.sv"));
  REQUIRE(!config->ShouldIncludeFile("rtl/module_tb.sv"));
  REQUIRE(!config->ShouldIncludeFile("tb/top_tb.sv"));
}

TEST_CASE("SlangdConfigFile mixed single and list patterns", "[config]") {
  const auto* content = R"(
If:
  PathMatch:
    - rtl/.*
    - common/.*
  PathExclude: .*/generated/.*
)";

  TempConfigFile temp(content);
  auto config = slangd::SlangdConfigFile::LoadFromFile(temp.Path());

  REQUIRE(config.has_value());

  // Should include: matches one of PathMatch list AND doesn't match single
  // PathExclude
  REQUIRE(config->ShouldIncludeFile("rtl/design.sv"));
  REQUIRE(config->ShouldIncludeFile("common/defines.sv"));

  // Should exclude: doesn't match PathMatch list
  REQUIRE(!config->ShouldIncludeFile("tb/testbench.sv"));

  // Should exclude: matches PathMatch list but matches PathExclude
  REQUIRE(!config->ShouldIncludeFile("rtl/generated/gen.sv"));
  REQUIRE(!config->ShouldIncludeFile("common/generated/gen.sv"));
}

TEST_CASE(
    "SlangdConfigFile empty PathMatch list includes everything", "[config]") {
  const auto* content = R"(
If:
  PathMatch: []
)";

  TempConfigFile temp(content);
  auto config = slangd::SlangdConfigFile::LoadFromFile(temp.Path());

  REQUIRE(config.has_value());

  // Empty list means no filtering
  REQUIRE(config->ShouldIncludeFile("rtl/design.sv"));
  REQUIRE(config->ShouldIncludeFile("tb/testbench.sv"));
  REQUIRE(config->ShouldIncludeFile("common/utils.sv"));
}

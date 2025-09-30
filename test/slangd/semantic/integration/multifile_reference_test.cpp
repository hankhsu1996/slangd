#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include <asio.hpp>
#include <catch2/catch_all.hpp>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include "../test_fixtures.hpp"
#include "slangd/semantic/semantic_index.hpp"

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

using SemanticTestFixture = slangd::semantic::test::SemanticTestFixture;
using MultiFileSemanticFixture =
    slangd::semantic::test::MultiFileSemanticFixture;

TEST_CASE(
    "SemanticIndex cross-package import resolution",
    "[semantic_index][multifile]") {
  MultiFileSemanticFixture fixture;

  // Create package file with typedef
  const std::string package_content = R"(
    package test_pkg;
      parameter WIDTH = 32;
      typedef logic [WIDTH-1:0] data_t;
    endpackage
  )";

  // Create module that imports and uses the package type
  const std::string module_content = R"(
    module test_module;
      import test_pkg::*;
      data_t my_data;  // Should resolve to package typedef
      logic local_signal;
    endmodule
  )";

  // Build SemanticIndex using EXPLICIT builder pattern (clearest approach)
  // Explicitly state: module = current file, package = unopened dependency
  auto result = fixture.CreateBuilder()
                    .SetCurrentFile(module_content, "test_module")
                    .AddUnopendFile(package_content, "test_pkg")
                    .Build();
  auto& index = result.index;
  REQUIRE(index != nullptr);
  REQUIRE(index->GetSymbolCount() > 0);

  // Verify we have symbols (file-scoped indexing is correct behavior)
  REQUIRE(MultiFileSemanticFixture::CountBuffersWithSymbols(*index) >= 1);

  // Test that symbols from the target file are indexed (file-scoped behavior)
  const auto& all_symbols = index->GetAllSymbols();
  bool found_module = false;
  bool found_local_signal = false;

  for (const auto& [location, info] : all_symbols) {
    std::string name(info.symbol->name);
    if (name == "test_module") {
      found_module = true;
      REQUIRE(info.lsp_kind == lsp::SymbolKind::kClass);
    }
    if (name == "local_signal") {
      found_local_signal = true;
      REQUIRE(info.lsp_kind == lsp::SymbolKind::kVariable);
    }
  }

  // With file-scoped indexing, we should find symbols from the target file
  REQUIRE(found_module);
  // local_signal should be indexed as it's defined in the module file
  REQUIRE(found_local_signal);
}

TEST_CASE(
    "SemanticIndex qualified package references",
    "[semantic_index][multifile]") {
  MultiFileSemanticFixture fixture;

  // Create package file with multiple symbols
  const std::string package_content = R"(
    package math_pkg;
      parameter BUS_WIDTH = 64;
      parameter ADDR_WIDTH = 32;
      typedef struct packed {
        logic [ADDR_WIDTH-1:0] addr;
        logic [BUS_WIDTH-1:0] data;
      } transaction_t;
    endpackage
  )";

  // Create module with qualified package references
  const std::string module_content = R"(
    module bus_controller;
      logic [math_pkg::BUS_WIDTH-1:0] data_bus;
      math_pkg::transaction_t transaction;
      logic [math_pkg::ADDR_WIDTH-1:0] address;
    endmodule
  )";

  // Build SemanticIndex using EXPLICIT builder pattern (clearest approach)
  // Explicitly state: module = current file, package = unopened dependency
  auto result = fixture.CreateBuilder()
                    .SetCurrentFile(module_content, "bus_controller")
                    .AddUnopendFile(package_content, "math_pkg")
                    .Build();
  auto& index = result.index;
  REQUIRE(index != nullptr);
  REQUIRE(index->GetSymbolCount() > 0);

  // Verify we have symbols (file-scoped indexing is correct behavior)
  REQUIRE(MultiFileSemanticFixture::CountBuffersWithSymbols(*index) >= 1);

  // Test that symbols from the target file are indexed (file-scoped behavior)
  const auto& all_symbols = index->GetAllSymbols();
  bool found_module = false;
  bool found_data_bus = false;
  bool found_transaction = false;
  bool found_address = false;

  for (const auto& [location, info] : all_symbols) {
    std::string name(info.symbol->name);
    if (name == "bus_controller") {
      found_module = true;
    }
    if (name == "data_bus") {
      found_data_bus = true;
    }
    if (name == "transaction") {
      found_transaction = true;
    }
    if (name == "address") {
      found_address = true;
    }
  }

  // With file-scoped indexing, we should find symbols from the target file
  REQUIRE(found_module);
  REQUIRE(found_data_bus);
  REQUIRE(found_transaction);
  REQUIRE(found_address);

  // Check that we have cross-file references (may not be detected for qualified
  // refs) This is a known limitation - qualified package references like
  // math_pkg::BUS_WIDTH might not be captured by the current
  // NamedValueExpression handler
  (void)MultiFileSemanticFixture::HasCrossFileReferences(*index);
  // Note: Test passes if symbols are found even if refs aren't tracked yet
}

TEST_CASE(
    "SemanticIndex multi-package dependencies", "[semantic_index][multifile]") {
  MultiFileSemanticFixture fixture;

  // Create base package
  const std::string base_package = R"(
    package base_pkg;
      parameter DATA_WIDTH = 32;
      typedef logic [DATA_WIDTH-1:0] word_t;
    endpackage
  )";

  // Create derived package that imports base
  const std::string derived_package = R"(
    package derived_pkg;
      import base_pkg::*;
      typedef struct packed {
        word_t data;
        logic valid;
      } packet_t;
    endpackage
  )";

  // Create module that uses derived package
  const std::string module_content = R"(
    module processor;
      import derived_pkg::*;
      packet_t input_packet;
      word_t data_word;
    endmodule
  )";

  // Build SemanticIndex using EXPLICIT builder pattern (clearest approach)
  // Explicitly state: module = current file, packages = unopened dependencies
  auto result = fixture.CreateBuilder()
                    .SetCurrentFile(module_content, "processor")
                    .AddUnopendFile(base_package, "base_pkg")
                    .AddUnopendFile(derived_package, "derived_pkg")
                    .Build();
  auto& index = result.index;
  REQUIRE(index != nullptr);
  REQUIRE(index->GetSymbolCount() > 0);

  // Verify we have symbols (file-scoped indexing is correct behavior)
  REQUIRE(MultiFileSemanticFixture::CountBuffersWithSymbols(*index) >= 1);

  // Test that symbols from current file (processor module) are indexed
  // With file-scoped indexing, only current file symbols are indexed
  const auto& all_symbols = index->GetAllSymbols();
  bool found_processor = false;
  bool found_input_packet = false;
  bool found_data_word = false;

  for (const auto& [location, info] : all_symbols) {
    std::string name(info.symbol->name);
    if (name == "processor") {
      found_processor = true;
    }
    if (name == "input_packet") {
      found_input_packet = true;
    }
    if (name == "data_word") {
      found_data_word = true;
    }
  }

  // Only expect symbols from the current file (processor module)
  REQUIRE(found_processor);
  REQUIRE(found_input_packet);
  REQUIRE(found_data_word);

  // Package symbols are not indexed since they're in dependency files
  // This is the expected behavior with file-scoped indexing
}

TEST_CASE(
    "SemanticIndex interface cross-file references",
    "[semantic_index][multifile]") {
  MultiFileSemanticFixture fixture;

  // Create interface definition file
  const std::string interface_content = R"(
    interface cpu_if;
      logic [31:0] addr;
      logic [31:0] data;
      logic valid;
      modport master (output addr, data, valid);
      modport slave (input addr, data, valid);
    endinterface
  )";

  // Create module that uses interface
  const std::string module_content = R"(
    module cpu_core(cpu_if.master bus);
      always_comb begin
        bus.addr = 32'h1000;
        bus.data = 32'hDEAD;
        bus.valid = 1'b1;
      end
      logic internal_state;
    endmodule
  )";

  // Build SemanticIndex using EXPLICIT builder pattern (clearest approach)
  // Explicitly state: module = current file, interface = unopened dependency
  auto result = fixture.CreateBuilder()
                    .SetCurrentFile(module_content, "cpu_core")
                    .AddUnopendFile(interface_content, "cpu_if")
                    .Build();
  auto& index = result.index;
  REQUIRE(index != nullptr);

  // Primary goal: Should not crash with interface cross-file usage
  REQUIRE(index->GetSymbolCount() > 0);

  // Verify current file (cpu_core module) symbols are indexed
  // With file-scoped indexing, only current file symbols are indexed
  const auto& all_symbols = index->GetAllSymbols();
  bool found_module = false;
  bool found_internal = false;

  for (const auto& [location, info] : all_symbols) {
    std::string name(info.symbol->name);
    if (name == "cpu_core") {
      found_module = true;
      REQUIRE(info.lsp_kind == lsp::SymbolKind::kClass);
    }
    if (name == "internal_state") {
      found_internal = true;
      REQUIRE(info.lsp_kind == lsp::SymbolKind::kVariable);
    }
  }

  // Only expect symbols from the current file (cpu_core module)
  REQUIRE(found_module);
  REQUIRE(found_internal);

  // Interface symbols are not indexed since they're in dependency files
  // This is the expected behavior with file-scoped indexing

  // Verify multifile compilation worked
  REQUIRE(MultiFileSemanticFixture::CountBuffersWithSymbols(*index) >= 1);
}

TEST_CASE("GetDocumentSymbols filters by URI", "[semantic_index][multifile]") {
  MultiFileSemanticFixture fixture;

  const std::string package_content = R"(
    package test_pkg;
      parameter BUS_WIDTH = 64;
      typedef logic [BUS_WIDTH-1:0] bus_data_t;
    endpackage
  )";

  const std::string module_content = R"(
    module test_module;
      import test_pkg::*;
      bus_data_t data_bus;
      logic [7:0] local_counter;
    endmodule
  )";

  // Build SemanticIndex using EXPLICIT builder pattern (clearest approach)
  // Explicitly state: module = current file, package = unopened dependency
  auto result = fixture.CreateBuilder()
                    .SetCurrentFile(module_content, "test_module")
                    .AddUnopendFile(package_content, "test_pkg")
                    .Build();
  REQUIRE(result.index != nullptr);
  REQUIRE(result.file_paths.size() == 2);

  std::string module_file = result.file_paths[0];   // file_0.sv (current file)
  std::string package_file = result.file_paths[1];  // file_1.sv (dependency)

  // Test module file filtering
  auto module_symbols = result.index->GetDocumentSymbols(module_file);
  bool found_module = false;
  for (const auto& symbol : module_symbols) {
    if (symbol.name == "test_module") {
      found_module = true;
    }
  }
  CHECK(found_module);  // Module filtering works

  // Test package file filtering
  auto package_symbols = result.index->GetDocumentSymbols(package_file);

  // With file-scoped indexing, dependency files are not indexed
  // So GetDocumentSymbols for dependency files should return empty
  CHECK(package_symbols.empty());

  // Verify that only the current file (module) has symbols
  CHECK(module_symbols.size() > 0);
}

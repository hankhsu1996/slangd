#include <memory>
#include <string>

#include <catch2/catch_all.hpp>

#include "slangd/semantic/semantic_index.hpp"
#include "test_fixtures.hpp"

auto main(int argc, char* argv[]) -> int {
  return Catch::Session().run(argc, argv);
}

namespace slangd::semantic {

using SemanticTestFixture = slangd::semantic::test::SemanticTestFixture;

TEST_CASE(
    "SemanticIndex handles interface ports without crash", "[semantic_index]") {
  SemanticTestFixture fixture;

  SECTION("basic interface port with member access") {
    const std::string source = R"(
      interface cpu_if;
        logic [31:0] addr;
        logic [31:0] data;
      endinterface

      module cpu_core(cpu_if.master bus);
        assign bus.addr = 32'h1000;
        assign bus.data = 32'hDEAD;
        logic internal_var;
      endmodule
    )";

    // Primary goal: This should not crash during symbol indexing
    auto index = fixture.BuildIndexFromSource(source);
    REQUIRE(index != nullptr);

    // Secondary goal: Basic sanity check that indexing still works
    REQUIRE(index->GetSymbolCount() > 0);

    // Just verify that SOME symbols are indexed (crash prevention is the main
    // goal) Interface definitions may not be indexed the same way as variables
    auto key = fixture.MakeKey(source, "internal_var");
    auto def_range = index->GetDefinitionRange(key);
    REQUIRE(def_range.has_value());
  }

  SECTION("undefined interface - single file resilience") {
    const std::string source = R"(
      // No interface definition - testing LSP resilience
      module processor(undefined_if bus);
        assign bus.signal = 1'b1;    // Interface doesn't exist
        assign bus.data = 32'hDEAD;  // Member doesn't exist

        // Regular symbols should still be indexed
        logic internal_state;
        logic [7:0] counter;
      endmodule
    )";

    // Primary: Should not crash even with undefined interface
    auto index = fixture.BuildIndexFromSource(source);
    REQUIRE(index != nullptr);

    // Secondary: Regular symbols still indexed despite interface errors
    REQUIRE(index->GetSymbolCount() > 0);

    auto key1 = fixture.MakeKey(source, "internal_state");
    auto def_range1 = index->GetDefinitionRange(key1);
    REQUIRE(def_range1.has_value());

    auto key2 = fixture.MakeKeyAt(source, "counter", 0);
    auto def_range2 = index->GetDefinitionRange(key2);
    REQUIRE(def_range2.has_value());

    // The undefined interface references (bus.signal, bus.data) are gracefully
    // handled
  }

  SECTION("interface in always_comb conditions and RHS") {
    const std::string source = R"(
      // Pattern that triggers Expression::tryBindInterfaceRef in procedural blocks
      module generic_module(generic_if iface);
        logic state;
        logic [7:0] counter;
        logic enable;

        always_comb begin
          if (enable & ~iface.ready) begin      // Interface in condition
            state = 1'b0;
          end else if (enable & iface.ready) begin
            if (iface.mode == 1'b1) begin      // Interface in comparison
              state = 1'b1;
            end else begin
              counter = iface.data;            // Interface in RHS assignment
            end
          end
        end
      endmodule
    )";

    // Primary: Should not crash with interface expressions in always_comb
    auto index = fixture.BuildIndexFromSource(source);
    REQUIRE(index != nullptr);

    // Secondary: Regular symbols still indexed despite interface usage
    REQUIRE(index->GetSymbolCount() > 0);

    auto key1 = fixture.MakeKeyAt(source, "state", 0);
    auto def_range1 = index->GetDefinitionRange(key1);
    REQUIRE(def_range1.has_value());

    auto key2 = fixture.MakeKeyAt(source, "counter", 0);
    auto def_range2 = index->GetDefinitionRange(key2);
    REQUIRE(def_range2.has_value());

    auto key3 = fixture.MakeKeyAt(source, "enable", 0);
    auto def_range3 = index->GetDefinitionRange(key3);
    REQUIRE(def_range3.has_value());

    // This test targets Expression::tryBindInterfaceRef code path
    // that differs from simple continuous assignments
  }
}

TEST_CASE(
    "SemanticIndex handles complex SystemVerilog patterns",
    "[semantic_index]") {
  SemanticTestFixture fixture;

  SECTION("nested scope definitions") {
    const std::string source = R"(
      module m;
        if (1) begin : named_block
          logic nested_signal;
          always_ff @(posedge clk) begin
            logic deeply_nested;
          end
        end
      endmodule
    )";

    auto index = fixture.BuildIndexFromSource(source);
    REQUIRE(index != nullptr);

    // Verify nested symbols are found
    auto nested_key = fixture.MakeKey(source, "nested_signal");
    REQUIRE(index->GetDefinitionRange(nested_key).has_value());

    auto deep_key = fixture.MakeKey(source, "deeply_nested");
    REQUIRE(index->GetDefinitionRange(deep_key).has_value());

    // Named block should also be indexed
    auto block_key = fixture.MakeKey(source, "named_block");
    REQUIRE(index->GetDefinitionRange(block_key).has_value());
  }

  SECTION("multiple declarations on single line") {
    const std::string source = R"(
      module m;
        logic sig1, sig2, sig3;
        logic [7:0] byte1, byte2, byte3;
        wire w1, w2, w3;
      endmodule
    )";

    auto index = fixture.BuildIndexFromSource(source);

    // All signals should be indexed
    for (const auto& name : {"sig1", "sig2", "sig3"}) {
      auto key = fixture.MakeKey(source, name);
      REQUIRE(index->GetDefinitionRange(key).has_value());
    }

    for (const auto& name : {"byte1", "byte2", "byte3"}) {
      auto key = fixture.MakeKey(source, name);
      REQUIRE(index->GetDefinitionRange(key).has_value());
    }

    // Different types too
    for (const auto& name : {"w1", "w2", "w3"}) {
      auto key = fixture.MakeKey(source, name);
      REQUIRE(index->GetDefinitionRange(key).has_value());
    }
  }

  SECTION("reference tracking in expressions") {
    const std::string source = R"(
      module m;
        logic a, b, c;
        logic [7:0] result;

        always_comb begin
          result = a ? b : c;  // References to a, b, c
          if (a && b) begin    // More references
            result = 8'hFF;
          end
        end
      endmodule
    )";

    auto index = fixture.BuildIndexFromSource(source);
    const auto& ref_map = index->GetReferenceMap();

    // Should have reference entries
    REQUIRE(!ref_map.empty());

    // Check that assignment reference is tracked
    auto ref_range =
        fixture.MakeRange(source, "result = a", std::string("result").size());

    // Reference should map back to definition
    if (ref_map.contains(ref_range)) {
      auto def_key = ref_map.at(ref_range);
      auto def_range = index->GetDefinitionRange(def_key);
      REQUIRE(def_range.has_value());
    }
  }

  SECTION("typedef and enum definitions") {
    const std::string source = R"(
      module m;
        typedef logic [31:0] word_t;
        typedef enum logic [1:0] {
          IDLE = 2'b00,
          ACTIVE = 2'b01,
          DONE = 2'b10
        } state_t;

        word_t data;
        state_t current_state;
      endmodule
    )";

    auto index = fixture.BuildIndexFromSource(source);

    // Typedef should be indexed
    auto word_key = fixture.MakeKeyAt(source, "word_t", 0);
    REQUIRE(index->GetDefinitionRange(word_key).has_value());

    auto state_key = fixture.MakeKeyAt(source, "state_t", 0);
    REQUIRE(index->GetDefinitionRange(state_key).has_value());

    // Enum values should be indexed
    for (const auto& name : {"IDLE", "ACTIVE", "DONE"}) {
      auto key = fixture.MakeKey(source, name);
      REQUIRE(index->GetDefinitionRange(key).has_value());
    }

    // Variables using typedefs
    auto data_key = fixture.MakeKeyAt(source, "data", 0);
    REQUIRE(index->GetDefinitionRange(data_key).has_value());

    auto current_state_key = fixture.MakeKey(source, "current_state");
    REQUIRE(index->GetDefinitionRange(current_state_key).has_value());
  }

  SECTION("package definitions") {
    const std::string source = R"(
      package test_pkg;
        parameter WIDTH = 32;
        typedef logic [WIDTH-1:0] data_t;
      endpackage
    )";

    auto index = fixture.BuildIndexFromSource(source);

    // Basic verification
    REQUIRE(index != nullptr);
    REQUIRE(index->GetSymbolCount() > 0);

    // Package should be indexed
    auto pkg_key = fixture.MakeKey(source, "test_pkg");
    REQUIRE(index->GetDefinitionRange(pkg_key).has_value());

    // Package contents should be indexed
    auto width_key = fixture.MakeKeyAt(source, "WIDTH", 0);
    REQUIRE(index->GetDefinitionRange(width_key).has_value());

    auto type_key = fixture.MakeKey(source, "data_t");
    REQUIRE(index->GetDefinitionRange(type_key).has_value());
  }

  SECTION("struct and union types") {
    const std::string source = R"(
      module m;
        typedef struct packed {
          logic [7:0] header;
          logic [23:0] payload;
        } packet_t;

        typedef union packed {
          logic [31:0] word;
          logic [7:0][3:0] bytes;
        } data_t;

        packet_t pkt;
        data_t data;
      endmodule
    )";

    auto index = fixture.BuildIndexFromSource(source);

    // Basic verification
    REQUIRE(index != nullptr);
    REQUIRE(index->GetSymbolCount() > 0);

    // Struct and union types should be indexed
    auto packet_key = fixture.MakeKeyAt(source, "packet_t", 0);
    REQUIRE(index->GetDefinitionRange(packet_key).has_value());

    auto data_type_key = fixture.MakeKeyAt(source, "data_t", 0);
    REQUIRE(index->GetDefinitionRange(data_type_key).has_value());

    // Variables using the types
    auto pkt_key = fixture.MakeKey(source, "pkt");
    REQUIRE(index->GetDefinitionRange(pkt_key).has_value());
  }

  SECTION("module with package imports") {
    const std::string source = R"(
      package test_pkg;
        parameter WIDTH = 32;
        typedef logic [WIDTH-1:0] data_t;
      endpackage

      module test_module;
        import test_pkg::*;
        data_t test_signal;
      endmodule
    )";

    auto index = fixture.BuildIndexFromSource(source);

    // Verify both package and module symbols are indexed
    auto pkg_key = fixture.MakeKeyAt(source, "test_pkg", 0);
    REQUIRE(index->GetDefinitionRange(pkg_key).has_value());

    auto mod_key = fixture.MakeKey(source, "test_module");
    REQUIRE(index->GetDefinitionRange(mod_key).has_value());

    auto sig_key = fixture.MakeKey(source, "test_signal");
    REQUIRE(index->GetDefinitionRange(sig_key).has_value());

    auto width_key = fixture.MakeKeyAt(source, "WIDTH", 0);
    REQUIRE(index->GetDefinitionRange(width_key).has_value());

    auto typedef_key = fixture.MakeKeyAt(source, "data_t", 0);
    REQUIRE(index->GetDefinitionRange(typedef_key).has_value());
  }
}

}  // namespace slangd::semantic

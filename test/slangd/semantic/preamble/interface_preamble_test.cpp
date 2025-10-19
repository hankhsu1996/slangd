#include <cstdlib>
#include <string>

#include <asio.hpp>
#include <catch2/catch_all.hpp>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include "../../common/async_fixture.hpp"
#include "../../common/multifile_semantic_fixture.hpp"

constexpr auto kLogLevel = spdlog::level::debug;

auto main(int argc, char* argv[]) -> int {
  spdlog::set_level(kLogLevel);
  spdlog::set_pattern("[%l] %v");

  setenv("TEST_SHARD_INDEX", "0", 0);
  setenv("TEST_TOTAL_SHARDS", "1", 0);
  setenv("TEST_SHARD_STATUS_FILE", "", 0);

  return Catch::Session().run(argc, argv);
}

using slangd::test::MultiFileSemanticFixture;
using slangd::test::RunAsyncTest;
using Fixture = MultiFileSemanticFixture;

TEST_CASE(
    "Simple interface instantiation with cross-file preamble",
    "[interface][preamble]") {
  RunAsyncTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    Fixture fixture;

    const std::string def = R"(
      interface simple_if;
        logic clk;
        logic rst;
        logic [7:0] data;
      endinterface
    )";

    const std::string ref = R"(
      module dut;
        simple_if bus();
      endmodule
    )";

    fixture.CreateBufferIDOffset();
    fixture.CreateFile("simple_if.sv", def);
    fixture.CreateFile("dut.sv", ref);

    auto session = fixture.BuildSession("dut.sv", executor);
    Fixture::AssertNoErrors(*session);
    Fixture::AssertCrossFileDef(*session, ref, def, "simple_if", 0, 0);

    co_return;
  });
}

TEST_CASE(
    "Parameterized interface instantiation with cross-file preamble",
    "[interface][preamble][parameter]") {
  RunAsyncTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    Fixture fixture;

    const std::string def = R"(
      interface bus_if #(parameter int WIDTH = 32);
        logic clk;
        logic valid;
        logic [WIDTH-1:0] data;
      endinterface
    )";

    const std::string ref = R"(
      module processor;
        bus_if #(.WIDTH(64)) wide_bus();
        bus_if #(.WIDTH(16)) narrow_bus();
      endmodule
    )";

    fixture.CreateBufferIDOffset();
    fixture.CreateFile("bus_if.sv", def);
    fixture.CreateFile("processor.sv", ref);

    auto session = fixture.BuildSession("processor.sv", executor);
    Fixture::AssertNoErrors(*session);
    Fixture::AssertCrossFileDef(*session, ref, def, "bus_if", 0, 0);
    Fixture::AssertCrossFileDef(*session, ref, def, "bus_if", 1, 0);

    co_return;
  });
}

TEST_CASE(
    "Interface instance array with cross-file preamble",
    "[interface][preamble][array]") {
  RunAsyncTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    Fixture fixture;

    const std::string def = R"(
      interface channel_if;
        logic clk;
        logic [7:0] data;
        logic valid;
      endinterface
    )";

    const std::string ref = R"(
      module router;
        parameter NUM_PORTS = 4;
        channel_if ports[NUM_PORTS]();
      endmodule
    )";

    fixture.CreateBufferIDOffset();
    fixture.CreateFile("channel_if.sv", def);
    fixture.CreateFile("router.sv", ref);

    auto session = fixture.BuildSession("router.sv", executor);
    Fixture::AssertNoErrors(*session);
    Fixture::AssertCrossFileDef(*session, ref, def, "channel_if", 0, 0);

    co_return;
  });
}

TEST_CASE(
    "Interface as module port with cross-file preamble",
    "[interface][preamble][port]") {
  RunAsyncTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    Fixture fixture;

    const std::string def = R"(
      interface axi_if;
        logic awvalid;
        logic awready;
        logic [31:0] awaddr;
      endinterface
    )";

    const std::string ref = R"(
      module master (
        axi_if m_axi
      );
      endmodule
    )";

    fixture.CreateBufferIDOffset();
    fixture.CreateFile("axi_if.sv", def);
    fixture.CreateFile("master.sv", ref);

    auto session = fixture.BuildSession("master.sv", executor);
    Fixture::AssertNoErrors(*session);
    Fixture::AssertCrossFileDef(*session, ref, def, "axi_if", 0, 0);

    co_return;
  });
}

TEST_CASE(
    "Interface array as module port with cross-file preamble",
    "[interface][preamble][port][array]") {
  RunAsyncTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    Fixture fixture;

    const std::string def = R"(
      interface stream_if;
        logic valid;
        logic ready;
        logic [63:0] data;
      endinterface
    )";

    const std::string ref = R"(
      module arbiter (
        stream_if inputs[4],
        stream_if out
      );
      endmodule
    )";

    fixture.CreateBufferIDOffset();
    fixture.CreateFile("stream_if.sv", def);
    fixture.CreateFile("arbiter.sv", ref);

    auto session = fixture.BuildSession("arbiter.sv", executor);
    Fixture::AssertNoErrors(*session);
    Fixture::AssertCrossFileDef(*session, ref, def, "stream_if", 0, 0);
    Fixture::AssertCrossFileDef(*session, ref, def, "stream_if", 1, 0);

    co_return;
  });
}

TEST_CASE(
    "Interface with modport and cross-file preamble",
    "[interface][preamble][modport]") {
  RunAsyncTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    Fixture fixture;

    const std::string def = R"(
      interface handshake_if;
        logic req;
        logic ack;
        logic [7:0] data;

        modport master (output req, input ack, output data);
        modport slave (input req, output ack, input data);
      endinterface
    )";

    const std::string ref = R"(
      module requester (
        handshake_if.master m_if
      );
      endmodule
    )";

    fixture.CreateBufferIDOffset();
    fixture.CreateFile("handshake_if.sv", def);
    fixture.CreateFile("requester.sv", ref);

    auto session = fixture.BuildSession("requester.sv", executor);
    Fixture::AssertNoErrors(*session);
    Fixture::AssertCrossFileDef(*session, ref, def, "handshake_if", 0, 0);

    co_return;
  });
}

TEST_CASE(
    "Multiple interfaces with different parameters in cross-file preamble",
    "[interface][preamble][multiple]") {
  RunAsyncTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    Fixture fixture;

    const std::string def1 = R"(
      interface fifo_if #(parameter int DEPTH = 16);
        logic full;
        logic empty;
        logic [7:0] data;
      endinterface
    )";

    const std::string def2 = R"(
      interface memory_if #(parameter int ADDR_WIDTH = 32);
        logic [ADDR_WIDTH-1:0] addr;
        logic [31:0] data;
        logic we;
      endinterface
    )";

    const std::string ref = R"(
      module controller;
        fifo_if #(.DEPTH(32)) tx_fifo();
        fifo_if #(.DEPTH(8)) rx_fifo();
        memory_if #(.ADDR_WIDTH(16)) mem();
      endmodule
    )";

    fixture.CreateBufferIDOffset();
    fixture.CreateFile("fifo_if.sv", def1);
    fixture.CreateFile("memory_if.sv", def2);
    fixture.CreateFile("controller.sv", ref);

    auto session = fixture.BuildSession("controller.sv", executor);
    Fixture::AssertNoErrors(*session);
    Fixture::AssertCrossFileDef(*session, ref, def1, "fifo_if", 0, 0);
    Fixture::AssertCrossFileDef(*session, ref, def1, "fifo_if", 1, 0);
    Fixture::AssertCrossFileDef(*session, ref, def2, "memory_if", 0, 0);

    co_return;
  });
}

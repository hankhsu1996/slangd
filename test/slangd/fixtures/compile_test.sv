// compile_test.sv
// A SystemVerilog file with hierarchical design to test compilation functionality

// Top-level module that instantiates other modules
module compile_top #(
    parameter int DATA_WIDTH = 32,
    parameter int ADDR_WIDTH = 16
) (
    input logic clk,
    input logic rst_n,
    input logic [ADDR_WIDTH-1:0] addr,
    input logic [DATA_WIDTH-1:0] wdata,
    input logic wen,
    input logic ren,
    output logic [DATA_WIDTH-1:0] rdata,
    output logic valid
);
  // Internal signals
  logic [DATA_WIDTH-1:0] memory_rdata;
  logic memory_valid;
  logic [DATA_WIDTH-1:0] fifo_rdata;
  logic fifo_valid;
  logic [1:0] select;

  // Compute address region select
  assign select = addr[ADDR_WIDTH-1:ADDR_WIDTH-2];

  // Memory module instance
  memory #(
      .DATA_WIDTH(DATA_WIDTH),
      .ADDR_WIDTH(ADDR_WIDTH - 2)
  ) memory_inst (
      .clk  (clk),
      .rst_n(rst_n),
      .addr (addr[ADDR_WIDTH-3:0]),
      .wdata(wdata),
      .wen  (wen && select == 2'b00),
      .ren  (ren && select == 2'b00),
      .rdata(memory_rdata),
      .valid(memory_valid)
  );

  // FIFO module instance
  fifo #(
      .DATA_WIDTH(DATA_WIDTH),
      .DEPTH(16)
  ) fifo_inst (
      .clk  (clk),
      .rst_n(rst_n),
      .push (wen && select == 2'b01),
      .pop  (ren && select == 2'b01),
      .wdata(wdata),
      .rdata(fifo_rdata),
      .valid(fifo_valid)
  );

  // Output mux
  always_comb begin
    case (select)
      2'b00: begin
        rdata = memory_rdata;
        valid = memory_valid;
      end
      2'b01: begin
        rdata = fifo_rdata;
        valid = fifo_valid;
      end
      default: begin
        rdata = '0;
        valid = 1'b0;
      end
    endcase
  end

endmodule

// Memory module
module memory #(
    parameter int DATA_WIDTH = 32,
    parameter int ADDR_WIDTH = 14
) (
    input logic clk,
    input logic rst_n,
    input logic [ADDR_WIDTH-1:0] addr,
    input logic [DATA_WIDTH-1:0] wdata,
    input logic wen,
    input logic ren,
    output logic [DATA_WIDTH-1:0] rdata,
    output logic valid
);
  // Memory array
  logic [DATA_WIDTH-1:0] mem[2**ADDR_WIDTH];

  // Write logic
  always_ff @(posedge clk) begin
    if (wen) begin
      mem[addr] <= wdata;
    end
  end

  // Read logic
  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      rdata <= '0;
      valid <= 1'b0;
    end else begin
      valid <= ren;
      if (ren) begin
        rdata <= mem[addr];
      end
    end
  end
endmodule

// FIFO module
module fifo #(
    parameter int DATA_WIDTH = 32,
    parameter int DEPTH = 16
) (
    input logic clk,
    input logic rst_n,
    input logic push,
    input logic pop,
    input logic [DATA_WIDTH-1:0] wdata,
    output logic [DATA_WIDTH-1:0] rdata,
    output logic valid
);
  // FIFO array and pointers
  logic [DATA_WIDTH-1:0] fifo[DEPTH];
  logic [$clog2(DEPTH):0] wr_ptr, rd_ptr, count;

  // FIFO control logic
  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      wr_ptr <= '0;
      rd_ptr <= '0;
      count  <= '0;
      valid  <= 1'b0;
    end else begin
      // Default valid
      valid <= (count > 0) && pop;

      // Push operation
      if (push && count < DEPTH) begin
        fifo[wr_ptr[$clog2(DEPTH)-1:0]] <= wdata;
        wr_ptr <= wr_ptr + 1'b1;
        count <= count + (pop ? '0 : 1'b1);
      end

      // Pop operation
      if (pop && count > 0) begin
        rd_ptr <= rd_ptr + 1'b1;
        count  <= count - (push ? '0 : 1'b1);
      end
    end
  end

  // Read data
  assign rdata = fifo[rd_ptr[$clog2(DEPTH)-1:0]];

endmodule

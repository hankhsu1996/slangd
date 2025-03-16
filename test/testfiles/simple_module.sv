// simple_module.sv
// A basic SystemVerilog module for testing the LSP server

module simple_counter (
    input logic clk,
    input logic rst_n,
    output logic [7:0] count
);
  // Counter register
  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      count <= 8'h00;
    end else begin
      count <= count + 1'b1;
    end
  end

  // Some commented documentation for testing hover
  // This is a simple 8-bit counter module with synchronous reset
endmodule

// Define an interface for testing
interface counter_if;
  logic clk;
  logic rst_n;
  logic [7:0] count;
endinterface

// Another module that uses the counter
module simple_module (
    input logic clk,
    input logic rst_n,
    output logic [7:0] led
);
  // Instantiate the counter
  simple_counter counter_inst (
      .clk  (clk),
      .rst_n(rst_n),
      .count(led)
  );
endmodule

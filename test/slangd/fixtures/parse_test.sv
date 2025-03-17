// parse_test.sv
// A minimal SystemVerilog file for testing basic parsing functionality

// Simple module with minimal features - perfect for basic parsing tests
module parse_test (
    input  logic       clk,
    input  logic       rst_n,
    input  logic [7:0] data_in,
    output logic [7:0] data_out,
    output logic       valid
);
  // Basic register
  logic [7:0] data_reg;

  // Simple sequential logic
  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      data_reg <= 8'h00;
      valid <= 1'b0;
    end else begin
      data_reg <= data_in;
      valid <= 1'b1;
    end
  end

  // Simple continuous assignment
  assign data_out = data_reg;

  // Named begin-end block
  initial begin : init_block
    $display("Module initialized");
  end

endmodule

// Simple parameter module
module param_test #(
    parameter int WIDTH = 8
) (
    input  logic             enable,
    input  logic [WIDTH-1:0] in,
    output logic [WIDTH-1:0] out
);
  // Simple logic with parameterization
  assign out = enable ? in : {WIDTH{1'b0}};
endmodule

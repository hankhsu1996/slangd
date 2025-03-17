// syntax_test.sv
// A SystemVerilog file with diverse syntax constructs to test syntax tree parsing

// Module with multiple parameter types
module syntax_test #(
    parameter int WIDTH = 8,
    parameter bit ENABLE = 1,
    parameter type DATA_T = logic,
    localparam string NAME = "SyntaxTest"
) (
    input logic clk,
    input logic rst_n,
    input DATA_T [WIDTH-1:0] data_in,
    output DATA_T [WIDTH-1:0] data_out
);

  // Various declaration types
  logic [WIDTH-1:0] internal_reg;
  wire some_wire;
  enum {
    IDLE,
    BUSY,
    DONE
  } state;

  // Generate construct
  generate
    if (ENABLE) begin : gen_block
      always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
          internal_reg <= '0;
        end else begin
          internal_reg <= data_in;
        end
      end
    end else begin : alt_block
      assign internal_reg = data_in;
    end
  endgenerate

  // Include preprocessor directive
`ifdef SIMULATION
  // Assertions
  assert property (@(posedge clk) disable iff (!rst_n) data_in == 0 |=> internal_reg == 0);
`endif

  // Complex expression
  assign data_out = ENABLE ? internal_reg : {WIDTH{1'b0}};

  // Task definition
  task automatic reset_regs();
    internal_reg = '0;
  endtask

  // Function definition
  function automatic DATA_T [WIDTH-1:0] get_data();
    return internal_reg;
  endfunction

endmodule

// Interface definition
interface test_if #(
    parameter WIDTH = 8
);
  logic clk;
  logic rst_n;
  logic [WIDTH-1:0] data;

  modport master(output data, input clk, rst_n);

  modport slave(input data, clk, rst_n);
endinterface

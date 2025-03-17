// symbol_test.sv
// A SystemVerilog file with various symbol types to test symbol extraction

// Package declaration
package test_pkg;
  typedef enum logic [1:0] {
    RED,
    GREEN,
    BLUE
  } color_t;

  typedef struct packed {
    logic [7:0] r;
    logic [7:0] g;
    logic [7:0] b;
  } rgb_t;

  // Constants
  localparam int MaxCount = 100;

  // Functions in package
  function automatic color_t get_default_color();
    return GREEN;
  endfunction
endpackage

// Import package
import test_pkg::*;

// Module with various symbols
module symbol_module (
    input  logic   clk,
    input  logic   rst_n,
    input  color_t color_in,
    output rgb_t   rgb_out
);
  // Internal signals
  logic [7:0] counter;
  rgb_t rgb_reg;

  // Parameters
  parameter color_t INIT_COLOR = RED;

  // Class definition
  class packet;
    rand bit [7:0] data;
    bit [3:0] id;

    function new(bit [3:0] id_val = 0);
      id = id_val;
    endfunction

    function void display();
      // Method implementation
    endfunction
  endclass

  // Generate block with named block
  generate
    if (INIT_COLOR == RED) begin : g_red_block
      assign rgb_reg.r = 8'hFF;
      assign rgb_reg.g = 8'h00;
      assign rgb_reg.b = 8'h00;
    end else if (INIT_COLOR == GREEN) begin : g_green_block
      assign rgb_reg.r = 8'h00;
      assign rgb_reg.g = 8'hFF;
      assign rgb_reg.b = 8'h00;
    end else begin : g_blue_block
      assign rgb_reg.r = 8'h00;
      assign rgb_reg.g = 8'h00;
      assign rgb_reg.b = 8'hFF;
    end
  endgenerate

  // Counter logic
  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      counter <= 8'h00;
    end else if (counter == MaxCount) begin
      counter <= 8'h00;
    end else begin
      counter <= counter + 1'b1;
    end
  end

  // RGB output based on input color
  always_comb begin
    case (color_in)
      RED: begin
        rgb_out.r = 8'hFF;
        rgb_out.g = 8'h00;
        rgb_out.b = 8'h00;
      end
      GREEN: begin
        rgb_out.r = 8'h00;
        rgb_out.g = 8'hFF;
        rgb_out.b = 8'h00;
      end
      BLUE: begin
        rgb_out.r = 8'h00;
        rgb_out.g = 8'h00;
        rgb_out.b = 8'hFF;
      end
      default: begin
        rgb_out = rgb_reg;
      end
    endcase
  end

  // Assertions
  property count_overflow;
    @(posedge clk) disable iff (!rst_n) counter == MaxCount |=> counter == 0;
  endproperty

  assert_count :
  assert property (count_overflow);

endmodule

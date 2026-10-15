// SPDX-License-Identifier: GPL-3.0-only
// Functional buffer model only; routed clocks and timing require Vivado.
module BUFG(input wire I,output wire O);
    assign O=I;
endmodule

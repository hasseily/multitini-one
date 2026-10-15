// SPDX-License-Identifier: GPL-3.0-only
// Load a user ROM while the machine is held in reset; then synchronous reads.
// Runtime uploads retain MiSTer's one-edge, CE-qualified read latency. Never
// write while the CPU/video consumes this ROM. No Apple bytes are initialized.
module apple_iigs_runtime_rom #(parameter integer AW=12)(
    input wire clock, input wire ce,
    input wire [AW-1:0] address, output reg [7:0] q,
    input wire load_clk, input wire load_we,
    input wire [AW-1:0] load_addr, input wire [7:0] load_data
);
    (* ram_style="block" *) reg [7:0] data[0:(1<<AW)-1];
    always @(posedge load_clk) if (load_we) data[load_addr]<=load_data;
    always @(posedge clock) if (ce) q<=data[address];
endmodule

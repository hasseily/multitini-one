// SPDX-License-Identifier: GPL-3.0-only
// Original Multitini byte-enable RAM for the unchanged Nuked VRAM model.
// Synchronous write-first reads match the upstream spram configuration.
module vram_ip (
    input wire clock,
    input wire [7:0] address,
    input wire [31:0] byteena,
    input wire [255:0] data,
    input wire wren,
    output reg [255:0] q
);
    (* ram_style = "block" *) reg [255:0] memory [0:255];
    integer i;
    initial begin
        q = 0;
        for (i = 0; i < 256; i = i + 1) memory[i] = 0;
    end
    integer lane;
    always @(posedge clock) begin
        q <= memory[address];
        for (lane = 0; lane < 32; lane = lane + 1)
            if (wren && byteena[lane]) begin
                memory[address][lane*8 +: 8] <= data[lane*8 +: 8];
                q[lane*8 +: 8] <= data[lane*8 +: 8];
            end
    end
endmodule

// Both ports share a clock. Port B is reserved for reset initialization;
// callers hold the machine in reset while it writes, so collisions cannot
// occur. Read-first latency is one cycle.
module multitini_genesis_ram #(
    parameter integer ADDRESS_WIDTH = 15,
    parameter integer DATA_WIDTH = 16
) (
    input wire clk,
    input wire [ADDRESS_WIDTH-1:0] address,
    input wire [DATA_WIDTH-1:0] data,
    input wire [DATA_WIDTH/8-1:0] byte_enable,
    input wire write_enable,
    output reg [DATA_WIDTH-1:0] q,
    input wire initialize,
    input wire [ADDRESS_WIDTH-1:0] initialize_address,
    input wire [DATA_WIDTH-1:0] initialize_data
);
    (* ram_style = "block" *) reg [DATA_WIDTH-1:0] memory [0:(1<<ADDRESS_WIDTH)-1];
    integer lane;
    always @(posedge clk) begin
        q <= memory[address];
        if (initialize) memory[initialize_address] <= initialize_data;
        else for (lane = 0; lane < DATA_WIDTH/8; lane = lane + 1)
            if (write_enable && byte_enable[lane])
                memory[address][lane*8 +: 8] <= data[lane*8 +: 8];
    end
endmodule

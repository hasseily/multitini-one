// SPDX-License-Identifier: GPL-3.0-only
// Original Multitini byte-enable RAM for the unchanged Nuked VRAM model.
// Synchronous write-first reads match the upstream spram configuration.
module vram_ip (
    input wire clock,
    input wire [7:0] address,
    input wire [31:0] byteena,
    input wire [255:0] data,
    input wire wren,
    output wire [255:0] q
);
    (* ram_style = "block" *) reg [255:0] memory [0:255];
    reg [255:0] read_data=0,write_data=0;
    reg [31:0] write_mask=0;
    integer i;
    initial begin
        for (i = 0; i < 256; i = i + 1) memory[i] = 0;
    end
    integer lane;
    always @(posedge clock) begin
        // Keep an unconditional synchronous RAM read. Register the forwarding
        // mux separately so partial byte writes do not turn it into LUT RAM.
        read_data <= memory[address];
        write_data <= data;
        write_mask <= byteena & {32{wren}};
        for (lane = 0; lane < 32; lane = lane + 1)
            if (wren && byteena[lane])
                memory[address][lane*8 +: 8] <= data[lane*8 +: 8];
    end
    genvar output_lane;
    generate for (output_lane=0;output_lane<32;output_lane=output_lane+1) begin: forwarding
        assign q[output_lane*8 +: 8] = write_mask[output_lane] ?
            write_data[output_lane*8 +: 8] : read_data[output_lane*8 +: 8];
    end endgenerate
endmodule

// One read port and one byte-enabled write port share a clock. Initialization
// selects the write address/data before the RAM, preserving one physical write
// port. Reads retain the old value on a same-address write (one-cycle latency).
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
    wire [ADDRESS_WIDTH-1:0] write_address = initialize ? initialize_address : address;
    wire [DATA_WIDTH-1:0] write_data = initialize ? initialize_data : data;
    wire [DATA_WIDTH/8-1:0] write_mask = initialize ? {DATA_WIDTH/8{1'b1}} :
        byte_enable & {DATA_WIDTH/8{write_enable}};
    integer lane;
    always @(posedge clk) begin
        q <= memory[address];
        for (lane = 0; lane < DATA_WIDTH/8; lane = lane + 1)
            if (write_mask[lane])
                memory[write_address][lane*8 +: 8] <= write_data[lane*8 +: 8];
    end
endmodule

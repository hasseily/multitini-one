// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// The CPU, VIC and DMA share the machine port; their scheduler owns ordering.
// PRG installation has priority and takes place while the machine is paused.
// Normalize the write address/data before the array: separate conditional
// writes to different indices caused the former array to infer LUT RAM.
module multitini_c64_local_ram (
    input  wire        clk,
    input  wire        loader_we,
    input  wire [15:0] loader_addr,
    input  wire [7:0]  loader_data,
    input  wire        machine_ce,
    input  wire        machine_we,
    input  wire        machine_store,
    input  wire [15:0] machine_addr,
    input  wire [7:0]  machine_data,
    output wire [7:0]  machine_q
);
    // No reset/initialization of RAM: reset must not erase guest memory.
    (* ram_style = "block" *) reg [7:0] ram [0:65535];
    wire write_enable = loader_we ||
                        (machine_ce && machine_we && machine_store);
    wire [15:0] write_address = loader_we ? loader_addr : machine_addr;
    wire [7:0] write_data = loader_we ? loader_data : machine_data;
    wire read_enable = !loader_we && machine_ce && !machine_we;
    reg [7:0] read_q = 8'd0;
    reg [7:0] write_q = 8'd0;
    reg write_response = 1'b0;

    // One synchronous read port and one write port infer a simple dual-port
    // BRAM. The shared scheduler never requests a simultaneous guest read and
    // write. Loader priority also prevents a cross-port same-address collision.
    always @(posedge clk) begin
        if (write_enable)
            ram[write_address] <= write_data;
        if (read_enable)
            read_q <= ram[machine_addr];
    end

    // Preserve the machine bus response on writes, including mapper-blocked
    // writes. It is a bus echo, not evidence that backing RAM accepted a store.
    // Loading and idle clocks hold the last response without touching BRAM q.
    always @(posedge clk) begin
        if (!loader_we && machine_ce) begin
            write_response <= machine_we;
            if (machine_we)
                write_q <= machine_data;
        end
    end
    assign machine_q = write_response ? write_q : read_q;
endmodule

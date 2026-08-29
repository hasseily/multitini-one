// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// Portable replacement for the upstream Intel altsyncram wrappers. Vivado
// must confirm the inferred memory shapes and read-during-write behavior.
module dpram #(
    parameter integer addr_width = 8,
    parameter integer data_width = 8,
    parameter string mem_init_file = ""
) (
    input  logic                    clock_a,
    input  logic [addr_width-1:0]   address_a,
    input  logic [data_width-1:0]   data_a,
    input  logic                    wren_a,
    output logic [data_width-1:0]   q_a,
    input  logic                    clock_b,
    input  logic [addr_width-1:0]   address_b,
    input  logic [data_width-1:0]   data_b,
    input  logic                    wren_b,
    output logic [data_width-1:0]   q_b
);
    logic [data_width-1:0] memory [0:(1 << addr_width)-1];

    initial begin
        if (mem_init_file != "")
            $readmemh(mem_init_file, memory);
    end

    // Match the selected Intel wrapper: port A is an unregistered read port.
    always_comb q_a = memory[address_a];

    always_ff @(posedge clock_a) begin
        if (wren_a)
            memory[address_a] <= data_a;
    end

    always_ff @(posedge clock_b) begin
        if (wren_b)
            memory[address_b] <= data_b;
        q_b <= wren_b ? data_b : memory[address_b];
    end
endmodule

// The only selected mixed-width use is the 4096-byte bootstrap store: an
// 8-bit read port and a 16-bit optional override-write port on one clock.
module dpram_dif #(
    parameter integer addr_width_a = 12,
    parameter integer data_width_a = 8,
    parameter integer addr_width_b = 11,
    parameter integer data_width_b = 16,
    parameter string mem_init_file = ""
) (
    input  logic                     clock,
    input  logic [addr_width_a-1:0]  address_a,
    output logic [data_width_a-1:0]  q_a,
    input  logic [addr_width_b-1:0]  address_b,
    input  logic [data_width_b-1:0]  data_b,
    input  logic                     wren_b,
    output logic [data_width_b-1:0]  q_b
);
    logic [7:0] memory [0:(1 << addr_width_a)-1];
    logic [addr_width_a-1:0] byte_address_b;

    initial begin
        if (data_width_a != 8 || data_width_b != 16 ||
            addr_width_a != addr_width_b + 1)
            $error("unsupported dpram_dif geometry");
        if (mem_init_file != "")
            $readmemh(mem_init_file, memory);
    end

    always_comb byte_address_b = {address_b, 1'b0};

    always_comb begin
        q_a = memory[address_a];
        q_b = {memory[byte_address_b + 1'b1], memory[byte_address_b]};
    end

    always_ff @(posedge clock) begin
        if (wren_b) begin
            memory[byte_address_b] <= data_b[7:0];
            memory[byte_address_b + 1'b1] <= data_b[15:8];
        end
    end
endmodule

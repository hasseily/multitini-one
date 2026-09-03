// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// Xilinx replacement for the upstream Intel altsyncram wrappers. The upstream
// memories register both port addresses, bypass the optional output register,
// and select new data on a same-port write collision. XPM latency 1 with
// write-first mode is the corresponding Xilinx block-RAM behavior.
module dpram_ce #(
    parameter integer addr_width = 8,
    parameter integer data_width = 8,
    parameter string mem_init_file = ""
) (
    input  logic                    clock_a,
    input  logic                    clken_a,
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
`ifdef SYNTHESIS
    localparam string XPM_INIT_FILE =
        (mem_init_file == "") ? "none" : mem_init_file;

    xpm_memory_tdpram #(
        .ADDR_WIDTH_A(addr_width),
        .ADDR_WIDTH_B(addr_width),
        .AUTO_SLEEP_TIME(0),
        .BYTE_WRITE_WIDTH_A(data_width),
        .BYTE_WRITE_WIDTH_B(data_width),
        .CLOCKING_MODE("independent_clock"),
        .ECC_MODE("no_ecc"),
        .MEMORY_INIT_FILE(XPM_INIT_FILE),
        .MEMORY_INIT_PARAM("0"),
        .MEMORY_OPTIMIZATION("true"),
        .MEMORY_PRIMITIVE("block"),
        .MEMORY_SIZE(data_width * (1 << addr_width)),
        .MESSAGE_CONTROL(0),
        .READ_DATA_WIDTH_A(data_width),
        .READ_DATA_WIDTH_B(data_width),
        .READ_LATENCY_A(1),
        .READ_LATENCY_B(1),
        .READ_RESET_VALUE_A("0"),
        .READ_RESET_VALUE_B("0"),
        .RST_MODE_A("SYNC"),
        .RST_MODE_B("SYNC"),
        .SIM_ASSERT_CHK(1),
        .USE_EMBEDDED_CONSTRAINT(0),
        .USE_MEM_INIT(1),
        .WAKEUP_TIME("disable_sleep"),
        .WRITE_DATA_WIDTH_A(data_width),
        .WRITE_DATA_WIDTH_B(data_width),
        .WRITE_MODE_A("write_first"),
        .WRITE_MODE_B("write_first")
    ) memory_i (
        .dbiterra(), .dbiterrb(), .douta(q_a), .doutb(q_b),
        .sbiterrb(), .sbiterra(),
        .addra(address_a), .addrb(address_b),
        .clka(clock_a), .clkb(clock_b),
        .dina(data_a), .dinb(data_b),
        .ena(clken_a), .enb(1'b1),
        .injectdbiterra(1'b0), .injectdbiterrb(1'b0),
        .injectsbiterra(1'b0), .injectsbiterrb(1'b0),
        .regcea(1'b1), .regceb(1'b1),
        .rsta(1'b0), .rstb(1'b0), .sleep(1'b0),
        .wea(wren_a), .web(wren_b)
    );
`else
    logic [data_width-1:0] memory [0:(1 << addr_width)-1];

    initial begin
        if (mem_init_file != "")
            $readmemh(mem_init_file, memory);
    end

    always_ff @(posedge clock_a) begin
        if (clken_a) begin
            if (wren_a) begin
                memory[address_a] <= data_a;
                q_a <= data_a;
            end else begin
                q_a <= memory[address_a];
            end
        end
    end

    always_ff @(posedge clock_b) begin
        if (wren_b) begin
            memory[address_b] <= data_b;
            q_b <= data_b;
        end else begin
            q_b <= memory[address_b];
        end
    end
`endif
endmodule

// Always-enabled compatibility wrapper for upstream instances that already
// use clk_sys directly. CPU-facing memories instantiate dpram_ce so their
// original clock-enable behavior does not create a fabric-gated clock.
module dpram #(
    parameter integer addr_width = 8,
    parameter integer data_width = 8
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
    dpram_ce #(
        .addr_width(addr_width),
        .data_width(data_width),
        .mem_init_file("")
    ) memory_i (
        .clock_a(clock_a), .clken_a(1'b1),
        .address_a(address_a), .data_a(data_a),
        .wren_a(wren_a), .q_a(q_a),
        .clock_b(clock_b), .address_b(address_b),
        .data_b(data_b), .wren_b(wren_b), .q_b(q_b)
    );
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
    initial begin
        if (data_width_a != 8 || data_width_b != 16 ||
            addr_width_a != addr_width_b + 1)
            $error("unsupported dpram_dif geometry");
    end

`ifdef SYNTHESIS
    localparam string XPM_INIT_FILE =
        (mem_init_file == "") ? "none" : mem_init_file;

    xpm_memory_tdpram #(
        .ADDR_WIDTH_A(addr_width_a),
        .ADDR_WIDTH_B(addr_width_b),
        .AUTO_SLEEP_TIME(0),
        .BYTE_WRITE_WIDTH_A(data_width_a),
        .BYTE_WRITE_WIDTH_B(data_width_b),
        .CLOCKING_MODE("common_clock"),
        .ECC_MODE("no_ecc"),
        .MEMORY_INIT_FILE(XPM_INIT_FILE),
        .MEMORY_INIT_PARAM("0"),
        .MEMORY_OPTIMIZATION("true"),
        .MEMORY_PRIMITIVE("block"),
        .MEMORY_SIZE(data_width_a * (1 << addr_width_a)),
        .MESSAGE_CONTROL(0),
        .READ_DATA_WIDTH_A(data_width_a),
        .READ_DATA_WIDTH_B(data_width_b),
        .READ_LATENCY_A(1),
        .READ_LATENCY_B(1),
        .READ_RESET_VALUE_A("0"),
        .READ_RESET_VALUE_B("0"),
        .RST_MODE_A("SYNC"),
        .RST_MODE_B("SYNC"),
        .SIM_ASSERT_CHK(1),
        .USE_EMBEDDED_CONSTRAINT(0),
        .USE_MEM_INIT(1),
        .WAKEUP_TIME("disable_sleep"),
        .WRITE_DATA_WIDTH_A(data_width_a),
        .WRITE_DATA_WIDTH_B(data_width_b),
        .WRITE_MODE_A("write_first"),
        .WRITE_MODE_B("write_first")
    ) memory_i (
        .dbiterra(), .dbiterrb(), .douta(q_a), .doutb(q_b),
        .sbiterrb(), .sbiterra(),
        .addra(address_a), .addrb(address_b),
        .clka(clock), .clkb(clock),
        .dina({data_width_a{1'b0}}), .dinb(data_b),
        .ena(1'b1), .enb(1'b1),
        .injectdbiterra(1'b0), .injectdbiterrb(1'b0),
        .injectsbiterra(1'b0), .injectsbiterrb(1'b0),
        .regcea(1'b1), .regceb(1'b1),
        .rsta(1'b0), .rstb(1'b0), .sleep(1'b0),
        .wea(1'b0), .web(wren_b)
    );
`else
    logic [7:0] memory [0:(1 << addr_width_a)-1];
    logic [addr_width_a-1:0] byte_address_b;

    initial begin
        if (mem_init_file != "")
            $readmemh(mem_init_file, memory);
    end

    always_comb byte_address_b = {address_b, 1'b0};

    always_ff @(posedge clock) begin
        if (wren_b) begin
            memory[byte_address_b] <= data_b[7:0];
            memory[byte_address_b + 1'b1] <= data_b[15:8];
            q_b <= data_b;
        end else begin
            q_b <= {memory[byte_address_b + 1'b1],
                    memory[byte_address_b]};
        end
        if (wren_b && address_a[addr_width_a-1:1] == address_b)
            q_a <= address_a[0] ? data_b[15:8] : data_b[7:0];
        else
            q_a <= memory[address_a];
    end
`endif
endmodule

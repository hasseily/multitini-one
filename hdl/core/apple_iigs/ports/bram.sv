// SPDX-License-Identifier: GPL-3.0-only
// Multitini inferred replacement for MiSTer's rtl/dpram_bram.vhd altsyncram.
// Both enabled ports have one edge read latency and same-port write-first
// behavior, matching NEW_DATA_NO_NBE_READ. Different-clock address collisions
// are undefined by the hardware; clients must not concurrently write a byte.
// Zero power-up contents match power_up_uninitialized=FALSE.
module bram #(
    parameter integer width_a=8,
    parameter integer widthad_a=10
) (
    input wire clock_a, input wire enable_a=1'b1, input wire wren_a,
    input wire [widthad_a-1:0] address_a,
    input wire [width_a-1:0] data_a,
    output reg [width_a-1:0] q_a,
    input wire clock_b, input wire enable_b=1'b1, input wire wren_b,
    input wire [widthad_a-1:0] address_b,
    input wire [width_a-1:0] data_b,
    output reg [width_a-1:0] q_b
);
    // The two writers implement a true dual-port BRAM, rather than registers.
    /* verilator lint_off MULTIDRIVEN */
    (* ram_style="block" *) reg [width_a-1:0] mem[0:(1<<widthad_a)-1];
    /* verilator lint_on MULTIDRIVEN */
    initial begin
        for (integer index=0; index<(1<<widthad_a); index=index+1) mem[index]=0;
        q_a=0; q_b=0;
    end
    always @(posedge clock_a) if (enable_a) begin
        if (wren_a) begin mem[address_a]<=data_a; q_a<=data_a; end
        else q_a<=mem[address_a];
    end
    always @(posedge clock_b) if (enable_b) begin
        if (wren_b) begin mem[address_b]<=data_b; q_b<=data_b; end
        else q_b<=mem[address_b];
    end
endmodule

// Only the 128KiB slow/video RAM needs explicit depth banking. An unbanked
// 128Kx8 array can infer 7-series hard cascades whose optimized address nets
// violate REQP-1962. Each 16Kx8 bank instead maps to parallel 16Kx2 RAMB36s,
// without extension cascades. Preserve the bank boundaries during synthesis.
// The selected bank follows each enabled read edge independently on A/B;
// retaining it while disabled also retains the observable output, exactly
// like bram. The bank's registered data and this selector change together,
// so this adds no read cycle. Same-byte cross-clock collisions remain undefined.
module apple_iigs_slowram (
    input wire clock_a, input wire enable_a=1'b1, input wire wren_a,
    input wire [16:0] address_a,
    input wire [7:0] data_a, output wire [7:0] q_a,
    input wire clock_b, input wire enable_b=1'b1, input wire wren_b,
    input wire [16:0] address_b,
    input wire [7:0] data_b, output wire [7:0] q_b
);
    reg [2:0] bank_a=0, bank_b=0;
    wire [7:0] bank_q_a [0:7];
    wire [7:0] bank_q_b [0:7];
    always @(posedge clock_a) if (enable_a) bank_a<=address_a[16:14];
    always @(posedge clock_b) if (enable_b) bank_b<=address_b[16:14];
    assign q_a=bank_q_a[bank_a];
    assign q_b=bank_q_b[bank_b];
    for (genvar index=0; index<8; index=index+1) begin:g_bank
        (* keep_hierarchy="yes" *) bram #(.widthad_a(14)) ram (
            .clock_a(clock_a), .enable_a(enable_a && address_a[16:14]==3'(index)),
            .wren_a(wren_a), .address_a(address_a[13:0]), .data_a(data_a), .q_a(bank_q_a[index]),
            .clock_b(clock_b), .enable_b(enable_b && address_b[16:14]==3'(index)),
            .wren_b(wren_b), .address_b(address_b[13:0]), .data_b(data_b), .q_b(bank_q_b[index])
        );
    end
endmodule

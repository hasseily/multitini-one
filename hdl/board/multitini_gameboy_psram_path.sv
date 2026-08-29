// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// Prepared GBC external-memory path. This block is excluded from active pin
// drive by default and must pass Vivado timing, CDC, and board PSRAM tests
// before ENABLE_GBC_PSRAM is used for a hardware image.
module multitini_gameboy_psram_path (
    input  logic         core_clk,
    input  logic         core_resetn,
    input  logic         control_clk,
    input  logic         control_resetn,
    input  logic         idelay_ref_clk,

    input  logic [23:0]  core_memory_addr,
    input  logic         core_memory_write,
    input  logic [63:0]  core_memory_wdata,
    input  logic [7:0]   core_memory_wstrb,
    input  logic         core_memory_valid,
    output logic         core_memory_ready,
    output logic [63:0]  core_memory_rdata,
    output logic         core_memory_rvalid,
    output logic         memory_fault,

    inout  wire [3:0]    psram_a_io,
    inout  wire [3:0]    psram_b_io,
    output logic         psram_ce_n,
    output logic         psram_clk
);
    logic idelay_ready;
    logic memory_resetn;
    logic [23:0] service_addr;
    logic service_write;
    logic [63:0] service_wdata;
    logic [7:0] service_wstrb;
    logic service_valid;
    logic service_ready;
    logic [63:0] service_rdata;
    logic service_rvalid;
    logic service_initialized;

    logic command_valid;
    logic command_ready;
    logic [7:0] command;
    logic [23:0] command_addr;
    logic [63:0] command_wdata;
    logic driver_response_valid;
    logic [63:0] driver_response_rdata;
    logic [3:0] psram_oe;
    logic [3:0] psram_a_i;
    logic [3:0] psram_a_o;
    logic [3:0] psram_b_i;
    logic [3:0] psram_b_o;

    IDELAYCTRL idelayctrl_i (
        .REFCLK(idelay_ref_clk), .RST(!control_resetn), .RDY(idelay_ready)
    );
    assign memory_resetn = control_resetn && idelay_ready;

    multitini_memory_cdc cdc_i (
        .core_clk(core_clk), .core_resetn(core_resetn),
        .core_request_addr(core_memory_addr),
        .core_request_write(core_memory_write),
        .core_request_wdata(core_memory_wdata),
        .core_request_wstrb(core_memory_wstrb),
        .core_request_valid(core_memory_valid),
        .core_request_ready(core_memory_ready),
        .core_response_rdata(core_memory_rdata),
        .core_response_valid(core_memory_rvalid),
        .memory_clk(control_clk), .memory_resetn(memory_resetn),
        .memory_request_addr(service_addr),
        .memory_request_write(service_write),
        .memory_request_wdata(service_wdata),
        .memory_request_wstrb(service_wstrb),
        .memory_request_valid(service_valid),
        .memory_request_ready(service_ready),
        .memory_response_rdata(service_rdata),
        .memory_response_valid(service_rvalid)
    );

    multitini_psram_line_service service_i (
        .clk(control_clk), .resetn(memory_resetn),
        .initialized(service_initialized), .fault(memory_fault),
        .request_addr(service_addr), .request_write(service_write),
        .request_wdata(service_wdata), .request_wstrb(service_wstrb),
        .request_valid(service_valid), .request_ready(service_ready),
        .response_rdata(service_rdata), .response_valid(service_rvalid),
        .command_valid(command_valid), .command_ready(command_ready),
        .command(command), .command_addr(command_addr),
        .command_wdata(command_wdata),
        .driver_response_valid(driver_response_valid),
        .driver_response_rdata(driver_response_rdata)
    );

    psram_driver driver_i (
        .clk(control_clk), .resetn(memory_resetn),
        .valid(command_valid), .ready(command_ready), .cmd(command),
        .addr(command_addr), .wdata(command_wdata),
        .rvalid(driver_response_valid), .rdata(driver_response_rdata),
        .done(), .dcount_wr_en(1'b0), .dcount_wr(5'd0),
        .dcount_edge(1'b0), .psram_oe(psram_oe),
        .psram_a_i(psram_a_i), .psram_a_o(psram_a_o),
        .psram_b_i(psram_b_i), .psram_b_o(psram_b_o),
        .psram_ce_n(psram_ce_n), .psram_clk(psram_clk)
    );

    genvar index;
    generate
        for (index = 0; index < 4; index = index + 1) begin : g_psram_iobuf
            IOBUF a_iobuf (
                .I(psram_a_o[index]), .O(psram_a_i[index]),
                .T(!psram_oe[index]), .IO(psram_a_io[index])
            );
            IOBUF b_iobuf (
                .I(psram_b_o[index]), .O(psram_b_i[index]),
                .T(!psram_oe[index]), .IO(psram_b_io[index])
            );
        end
    endgenerate

    wire unused_initialized = service_initialized;
endmodule

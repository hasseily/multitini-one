// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps
module multitini_c64_uci_transport (
    input logic host_clk,host_resetn,
    input logic [31:0] host_op,
    input logic host_op_valid,
    output wire host_op_ready,
    input logic [8:0] host_read_addr,host_write_addr,
    input logic [31:0] host_write_data,
    input logic [3:0] host_write_strobe,
    output logic [31:0] host_read_data,
    input logic core_clk,core_resetn,
    output wire [31:0] core_op,
    output wire core_op_valid,
    input logic core_op_ready,
    input logic [10:0] core_addr,
    input logic core_write,
    input logic [7:0] core_write_data,
    output logic [7:0] core_read_data
);
    multitini_async_mailbox #(.WIDTH(32),.DRAIN_ON_RESET(1)) op_i(
        .src_clk(host_clk),.src_resetn(host_resetn),.src_data(host_op),
        .src_valid(host_op_valid),.src_ready(host_op_ready),
        .dst_clk(core_clk),.dst_resetn(core_resetn),.dst_data(core_op),
        .dst_valid(core_op_valid),.dst_ready(core_op_ready));
    wire [8:0] host_addr=(|host_write_strobe)?host_write_addr:host_read_addr;
`ifdef SYNTHESIS
    xpm_memory_tdpram #(
        .ADDR_WIDTH_A(11),.ADDR_WIDTH_B(9),.AUTO_SLEEP_TIME(0),
        .BYTE_WRITE_WIDTH_A(8),.BYTE_WRITE_WIDTH_B(8),
        .CLOCKING_MODE("independent_clock"),.ECC_MODE("no_ecc"),
        .MEMORY_INIT_FILE("none"),.MEMORY_INIT_PARAM("0"),
        .MEMORY_OPTIMIZATION("true"),.MEMORY_PRIMITIVE("block"),
        .MEMORY_SIZE(16384),.MESSAGE_CONTROL(0),
        .READ_DATA_WIDTH_A(8),.READ_DATA_WIDTH_B(32),
        .READ_LATENCY_A(1),.READ_LATENCY_B(1),
        .READ_RESET_VALUE_A("0"),.READ_RESET_VALUE_B("0"),
        .RST_MODE_A("SYNC"),.RST_MODE_B("SYNC"),.SIM_ASSERT_CHK(1),
        .USE_EMBEDDED_CONSTRAINT(0),.USE_MEM_INIT(0),.WAKEUP_TIME("disable_sleep"),
        .WRITE_DATA_WIDTH_A(8),.WRITE_DATA_WIDTH_B(32),
        .WRITE_MODE_A("read_first"),.WRITE_MODE_B("read_first")
    ) ram_i(
        .clka(core_clk),.clkb(host_clk),.ena(1'b1),.enb(1'b1),
        .addra(core_addr),.addrb(host_addr),.dina(core_write_data),.dinb(host_write_data),
        .wea(core_write),.web(host_write_strobe),.douta(core_read_data),.doutb(host_read_data),
        .regcea(1'b1),.regceb(1'b1),.rsta(1'b0),.rstb(1'b0),.sleep(1'b0),
        .injectdbiterra(1'b0),.injectdbiterrb(1'b0),.injectsbiterra(1'b0),.injectsbiterrb(1'b0),
        .dbiterra(),.dbiterrb(),.sbiterra(),.sbiterrb());
`else
    logic [7:0] memory[0:2047];
    always @(posedge core_clk) begin
        if(core_write)memory[core_addr]<=core_write_data;
        core_read_data<=memory[core_addr];
    end
    always @(posedge host_clk) begin
        for(integer lane=0;lane<4;lane=lane+1)begin
            if(host_write_strobe[lane])memory[{host_addr,2'b00}+lane]<=host_write_data[lane*8+:8];
            host_read_data[lane*8+:8]<=memory[{host_addr,2'b00}+lane];
        end
    end
`endif
endmodule

// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/100ps

module tb_apple_iigs_scc;
    logic clk = 0;
    logic reset = 1;
    logic cs = 0;
    logic we = 0;
    logic [1:0] rs = 0;
    logic [7:0] wdata = 0;
    wire [7:0] rdata;
    logic [7:0] status;

    always #5 clk = ~clk;

    scc_iigs_wrapper dut (
        .clk_14m(clk), .ph0_en(1'b0), .ph2_en(1'b0), .q3_en(1'b0),
        .reset(reset), .cs(cs), .we(we), .rs(rs), .wdata(wdata),
        .rdata(rdata), .irq_n(),
        .txd_a(), .txd_b(), .rxd_a(1'b1), .rxd_b(1'b1),
        .rts_a(), .rts_b(), .cts_a(1'b0), .cts_b(1'b0), .dsr_a(1'b1)
    );

    task automatic write_control(input logic channel_a, input logic [7:0] value);
        @(negedge clk);
        rs = {1'b0, channel_a};
        wdata = value;
        we = 1;
        cs = 1;
        @(negedge clk);
        cs = 0;
    endtask

    task automatic read_control(input logic channel_a, output logic [7:0] value);
        @(negedge clk);
        rs = {1'b0, channel_a};
        we = 0;
        cs = 1;
        #1 value = rdata;
        @(negedge clk);
        cs = 0;
    endtask

    task automatic check_wr0(input logic channel_a);
        read_control(channel_a, status);
        if (status == 0) $fatal(1, "RR0 baseline must differ from RR3");
        write_control(channel_a, 8'h10); // WR0 Reset External/Status Interrupts.
        write_control(channel_a, 8'h03); // Select RR3 on the very next access.
        read_control(channel_a, status);
        if (status != 0) $fatal(1, "WR0 command swallowed RR3 selection on channel %b", channel_a);
    endtask

    task automatic check_wr9;
        write_control(1'b1, 8'h0f); // Point High and select WR15.
        write_control(1'b1, 8'h28);
        write_control(1'b0, 8'h0f);
        write_control(1'b0, 8'ha0);
        write_control(1'b1, 8'h09); // Point High and select WR9.
        @(negedge clk);
        // The write decode pulses between clock edges, as can happen when
        // the register pointer and chip select take different paths.
        wdata = 8'hc0;
        #1 cs = 1;
        #1 cs = 0;
        // A normal read retires the selection without issuing a reset.
        read_control(1'b1, status);
        write_control(1'b1, 8'h0f);
        read_control(1'b1, status);
        if (status != 8'h28) $fatal(1, "Unsampled WR9 reset changed channel A WR15 to %h", status);
        write_control(1'b0, 8'h0f);
        read_control(1'b0, status);
        if (status != 8'ha0) $fatal(1, "Unsampled WR9 reset changed channel B WR15 to %h", status);

        write_control(1'b1, 8'h09);
        write_control(1'b1, 8'hc0); // A valid WR9 reset must still reach both channels.
        write_control(1'b1, 8'h0f);
        read_control(1'b1, status);
        if (status != 8'hf8) $fatal(1, "Valid WR9 reset did not restore channel A WR15");
        write_control(1'b0, 8'h0f);
        read_control(1'b0, status);
        if (status != 8'hf8) $fatal(1, "Valid WR9 reset did not restore channel B WR15");
    endtask

    initial begin
        repeat (4) @(negedge clk);
        reset = 0;
        if (!$test$plusargs("WR9_ONLY")) begin
            check_wr0(1'b1);
            check_wr0(1'b0);
        end
        check_wr9();
        $display("PASS: SCC WR0 pointer and synchronous WR9 reset");
        $finish;
    end
endmodule

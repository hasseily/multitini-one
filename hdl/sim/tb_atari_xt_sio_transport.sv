// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps

module tb_atari_xt_sio_transport;
    import multitini_regs_pkg::*;
    reg control_clk = 0, core_clk = 0;
    always #3.75 control_clk = ~control_clk;
    always #5 core_clk = ~core_clk;
    reg control_resetn = 0, core_resetn = 0, guest_reset = 1;
    reg reg_wr_en = 0, reg_rd_en = 0;
    reg [11:0] reg_wr_addr = 0, reg_rd_addr = MT_REG_ATARI_SIO_STATUS;
    reg [3:0] reg_wr_strb = 0;
    reg [31:0] reg_wr_data = 0;
    wire [31:0] reg_rd_data;
    reg event_valid = 0, reply_ready = 0;
    reg [17:0] event_data = 0;
    wire event_ready, reply_valid;
    wire [16:0] reply_data;

    multitini_atari_sio_transport dut (.*);

    task automatic write_reg(input [11:0] address, input [31:0] data, input [3:0] lanes);
        @(negedge control_clk);
        reg_wr_addr = address;
        reg_wr_data = data;
        reg_wr_strb = lanes;
        reg_wr_en = 1;
        @(negedge control_clk);
        reg_wr_en = 0;
    endtask

    task automatic expect_status(input [3:0] bits);
        @(negedge control_clk);
        reg_rd_addr = MT_REG_ATARI_SIO_STATUS;
        #1;
        if (reg_rd_data !== (MT_ATARI_SIO_STATUS_SIGNATURE | {28'd0, bits}))
            $fatal(1, "status bits %h, expected %h", reg_rd_data, bits);
    endtask

    task automatic send_event(input [17:0] data);
        @(negedge core_clk);
        event_data = data;
        event_valid = 1;
        do @(posedge core_clk); while (!event_ready);
        @(negedge core_clk);
        event_valid = 0;
    endtask

    task automatic expect_event(input [17:0] data);
        @(negedge control_clk);
        reg_rd_addr = MT_REG_ATARI_SIO_RX;
        #1;
        while (!reg_rd_data[31]) @(negedge control_clk);
        if (reg_rd_data !== (MT_ATARI_SIO_RX_VALID | {14'd0, data}))
            $fatal(1, "RX event %h, expected %h", reg_rd_data, data);
        reg_rd_en = 1;
        @(negedge control_clk);
        reg_rd_en = 0;
    endtask

    task automatic expect_reply(input [16:0] data);
        @(negedge core_clk);
        reply_ready = 1;
        do @(posedge core_clk); while (!reply_valid);
        if (reply_data !== data)
            $fatal(1, "TX reply %h, expected %h", reply_data, data);
        @(negedge core_clk);
        reply_ready = 0;
    endtask

    initial begin
        #2ms;
        $fatal(1, "SIO transport timeout");
    end

    initial begin
        repeat (8) @(negedge core_clk);
        control_resetn = 1;
        core_resetn = 1;
        expect_status(4'b1000);
        write_reg(MT_REG_ATARI_SIO_TX, 32'h1234, 4'hf);
        @(negedge core_clk);
        guest_reset = 0;
        wait(event_ready);
        expect_status(4'b0010);

        for (int i = 0; i < 300; i++) send_event(18'h19400 + 18'(i));
        expect_status(4'b0011);
        for (int i = 0; i < 300; i++) expect_event(18'h19400 + 18'(i));
        expect_status(4'b0010);
        for (int i = 0; i < 300; i++) send_event(18'h20000 + 18'(i));
        for (int i = 0; i < 300; i++) expect_event(18'h20000 + 18'(i));
        expect_status(4'b0010);

        write_reg(MT_REG_ATARI_SIO_TX, 32'h40000000, 4'hf);
        expect_status(4'b0110);
        write_reg(MT_REG_ATARI_SIO_STATUS, MT_ATARI_SIO_STATUS_TX_FAULT, 4'h2);
        expect_status(4'b0110);
        write_reg(MT_REG_ATARI_SIO_STATUS, MT_ATARI_SIO_STATUS_TX_FAULT, 4'h1);
        expect_status(4'b0010);
        write_reg(MT_REG_ATARI_SIO_TX, 32'h12, 4'h1);
        expect_status(4'b0110);
        write_reg(MT_REG_ATARI_SIO_STATUS, MT_ATARI_SIO_STATUS_TX_FAULT, 4'hf);

        // The outstanding mailbox word plus all 512 queue entries fit.
        for (int i = 0; i < 513; i++) write_reg(MT_REG_ATARI_SIO_TX, 32'h10f00 + 32'(i), 4'hf);
        expect_status(4'b0000);
        write_reg(MT_REG_ATARI_SIO_TX, 32'hbad, 4'hf);
        expect_status(4'b0100);
        for (int i = 0; i < 513; i++) expect_reply(17'h10f00 + 17'(i));
        expect_status(4'b0110);

        // Queue stale words in both directions, then assert the shortest
        // guest reset. Recovered RX and TX may only return new payloads.
        send_event(18'h11111);
        write_reg(MT_REG_ATARI_SIO_TX, 32'h12222, 4'hf);
        write_reg(MT_REG_ATARI_SIO_TX, 32'h13333, 4'hf);
        wait(reply_valid);
        expect_status(4'b0111);
        @(negedge core_clk);
        guest_reset = 1;
        @(negedge core_clk);
        guest_reset = 0;
        wait (!dut.control_link_resetn);
        expect_status(4'b1000);
        wait(event_ready);
        expect_status(4'b0010);
        send_event(18'h24444);
        expect_event(18'h24444);
        write_reg(MT_REG_ATARI_SIO_TX, 32'h15555, 4'hf);
        expect_reply(17'h15555);
        repeat (32) @(negedge core_clk);
        if (reply_valid) $fatal(1, "stale or duplicate TX after recovery");
        expect_status(4'b0010);

        send_event(18'h17777);
        write_reg(MT_REG_ATARI_SIO_TX, 32'h16666, 4'hf);
        wait (reply_valid);
        @(negedge control_clk);
        control_resetn = 0;
        repeat (12) @(negedge control_clk);
        if (reply_valid || event_ready) $fatal(1, "core side ignored control reset");
        control_resetn = 1;
        wait (event_ready);
        expect_status(4'b0010);
        if (reply_valid) $fatal(1, "stale TX after control reset");
        $display("PASS Atari SIO transport queues, faults and reset recovery");
        $finish;
    end
endmodule

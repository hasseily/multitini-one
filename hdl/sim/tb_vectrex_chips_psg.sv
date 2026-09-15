// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Original register/audio stimulus; contains no sampled audio or ROM data.
module tb_vectrex_chips_psg;
    reg clk = 0;
    always #5 clk = ~clk;
    reg reset = 1, ce = 0, bdir = 0, bc = 0;
    reg [7:0] di = 0, ioa_in = 8'h96, iob_in = 8'h3c;
    wire [7:0] dout, dout_8910, dout_8913;
    wire [7:0] channel_a, channel_b, channel_c, ioa_out, iob_out;
    wire ioa_oen, iob_oen, ioa_oen_8910, iob_oen_8910;
    wire ioa_oen_8913, iob_oen_8913;
    multitini_vectrex_psg #(.IO_PORTS(1)) dut (
        .CLK(clk), .CE(ce), .RESET(reset), .BDIR(bdir), .BC(bc), .DI(di), .DO(dout),
        .CHANNEL_A(channel_a), .CHANNEL_B(channel_b), .CHANNEL_C(channel_c),
        .SEL(1'b0), .MODE(1'b1), .ACTIVE(), .IOA_in(ioa_in), .IOA_out(ioa_out),
        .IOA_OEn(ioa_oen), .IOB_in(iob_in), .IOB_out(iob_out), .IOB_OEn(iob_oen)
    );
    multitini_vectrex_psg #(.IO_PORTS(2)) psg_8910 (
        .CLK(clk), .CE(ce), .RESET(reset), .BDIR(bdir), .BC(bc), .DI(di), .DO(dout_8910),
        .CHANNEL_A(), .CHANNEL_B(), .CHANNEL_C(), .SEL(1'b0), .MODE(1'b1), .ACTIVE(),
        .IOA_in(ioa_in), .IOA_out(), .IOA_OEn(ioa_oen_8910),
        .IOB_in(iob_in), .IOB_out(), .IOB_OEn(iob_oen_8910)
    );
    multitini_vectrex_psg #(.IO_PORTS(0)) psg_8913 (
        .CLK(clk), .CE(ce), .RESET(reset), .BDIR(bdir), .BC(bc), .DI(di), .DO(dout_8913),
        .CHANNEL_A(), .CHANNEL_B(), .CHANNEL_C(), .SEL(1'b0), .MODE(1'b1), .ACTIVE(),
        .IOA_in(ioa_in), .IOA_out(), .IOA_OEn(ioa_oen_8913),
        .IOB_in(iob_in), .IOB_out(), .IOB_OEn(iob_oen_8913)
    );
    task settle;
        repeat (3) @(posedge clk);
        #1;
    endtask
    task select_reg(input [7:0] a);
        @(negedge clk); di = a; bdir = 1; bc = 1;
        @(negedge clk); bdir = 0; bc = 0;
        settle();
    endtask
    task write_reg(input [7:0] a, input [7:0] d);
        select_reg(a);
        @(negedge clk); di = d; bdir = 1; bc = 0;
        @(negedge clk); bdir = 0;
        settle();
    endtask
    task read_reg(input [7:0] a, input [7:0] expect_8912, input [7:0] expect_8910, input [7:0] expect_8913);
        select_reg(a);
        bc = 1; #1;
        if (dout !== expect_8912 || dout_8910 !== expect_8910 || dout_8913 !== expect_8913)
            $fatal(1, "PSG R%0d read %h/%h/%h expected %h/%h/%h", a,
                dout, dout_8910, dout_8913, expect_8912, expect_8910, expect_8913);
        bc = 0;
    endtask
    task tick;
        @(negedge clk); ce = 1;
        @(negedge clk); ce = 0;
        settle();
    endtask
    task reset_chip;
        @(negedge clk); reset = 1;
        settle();
        @(negedge clk); reset = 0;
        settle();
    endtask

    initial begin
        reset_chip();
        if (ioa_oen !== 1 || iob_oen !== 1 || channel_a !== 0 || channel_b !== 0 || channel_c !== 0)
            $fatal(1, "PSG reset must silence audio and select input pins");
        if (dout !== 8'hff) $fatal(1, "PSG inactive bus must read FF");
        write_reg(14, 8'ha5);
        write_reg(15, 8'hc3);
        read_reg(14, 8'h96, 8'h96, 8'ha5);
        read_reg(15, 8'hc3, 8'h3c, 8'hc3);
        write_reg(7, 8'hc0);
        if (ioa_oen !== 0 || iob_oen !== 1 || ioa_oen_8910 !== 0 || iob_oen_8910 !== 0 ||
            ioa_oen_8913 !== 1 || iob_oen_8913 !== 1)
            $fatal(1, "PSG I/O direction/package selection");
        if (ioa_out !== 8'ha5 || iob_out !== 8'hc3) $fatal(1, "PSG I/O output latches");
        read_reg(14, 8'h84, 8'h84, 8'ha5); // external low pins remain low
        read_reg(15, 8'hc3, 8'h00, 8'hc3);
        ioa_in = 8'hff; iob_in = 8'hff;
        read_reg(14, 8'ha5, 8'ha5, 8'ha5);
        write_reg(7, 0);
        ioa_in = 8'hf0;
        read_reg(14, 8'hf0, 8'hf0, 8'ha5);
        write_reg(1, 8'hff);
        read_reg(1, 8'h0f, 8'h0f, 8'h0f);
        read_reg(8'h10, 8'hff, 8'hff, 8'hff);

        // Tone period 3: square-wave edges every 8*3 input CE pulses.
        reset_chip();
        write_reg(0, 3);
        write_reg(1, 0);
        write_reg(7, 8'h3e); // enable A tone; disable noise and B/C tones
        write_reg(8, 15);
        begin : tone
            integer last_edge, edges;
            reg [7:0] old_sample;
            edges = 0; last_edge = 0; old_sample = channel_a;
            for (integer t = 1; t <= 200; t = t + 1) begin
                tick();
                if (channel_a !== 0 && channel_a !== 255) $fatal(1, "PSG tone unknown/invalid amplitude");
                if (channel_a != old_sample) begin
                    if (edges != 0 && t - last_edge != 24) $fatal(1, "PSG tone period got %0d CE", t-last_edge);
                    last_edge = t; edges = edges + 1; old_sample = channel_a;
                end
            end
            if (edges < 7) $fatal(1, "PSG tone failed to oscillate");
        end

        // Envelope 0 descends to silence and holds; a repeated R13 write
        // restarts it. Envelope D rises to full volume and holds.
        write_reg(7, 8'h3f);
        write_reg(8, 16);
        write_reg(11, 1);
        write_reg(12, 0);
        write_reg(13, 0);
        if (channel_a !== 255) $fatal(1, "PSG decay envelope initial peak");
        begin : decay
            reg [7:0] old_sample;
            integer changes;
            old_sample = channel_a; changes = 0;
            for (integer t = 0; t < 360; t = t + 1) begin
                tick();
                if ((^channel_a) === 1'bx || channel_a > old_sample) $fatal(1, "PSG decay envelope order");
                if (channel_a != old_sample) changes = changes + 1;
                old_sample = channel_a;
            end
            if (channel_a !== 0 || changes != 15) $fatal(1, "PSG decay envelope must cover 16 AY levels");
        end
        write_reg(13, 0);
        if (channel_a !== 255) $fatal(1, "PSG same-shape rewrite restart");
        write_reg(13, 13);
        if (channel_a !== 0) $fatal(1, "PSG attack envelope initial zero");
        begin : attack
            reg [7:0] old_sample;
            old_sample = 0;
            repeat (360) begin
                tick();
                if ((^channel_a) === 1'bx || channel_a < old_sample) $fatal(1, "PSG attack envelope order");
                old_sample = channel_a;
            end
            if (channel_a !== 255) $fatal(1, "PSG attack envelope hold");
        end
        reset_chip();
        if (channel_a !== 0 || channel_b !== 0 || channel_c !== 0) $fatal(1, "PSG reset silence after sound");
        $display("PASS: Vectrex AY8912 I/O, AY8913 compatibility, tone and envelope");
        $finish;
    end
    initial begin #2000000; $fatal(1, "PSG timeout"); end
endmodule

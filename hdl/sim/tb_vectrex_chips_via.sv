// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Original register-level stimulus; no system ROM or game data.
module tb_vectrex_chips_via;
    reg clk = 0;
    always #5 clk = ~clk;
    reg reset = 1, power_reset = 1, slow_clock = 0;
    reg strobe = 0, we = 0;
    reg [3:0] addr = 0;
    reg [7:0] data_in = 0;
    wire [7:0] data_out, porta_out, porta_bus, ddra_out;
    wire [7:0] portb_out, portb_bus, pcr_out, ddrb_out;
    wire ca2_out, cb1_out, cb2_out, irq;
    reg ca1_in = 1, cb1_in = 1, cb2_in = 1;
    reg [7:0] porta_in = 8'ha5, portb_in = 8'hff;

    multitini_vectrex_via6522 dut (
        .clk(clk), .reset(reset), .power_reset(power_reset),
        .slow_clock(slow_clock), .timer_read_extra_clock(1'b0),
        .strobe(strobe), .we(we), .addr(addr), .data_in(data_in),
        .data_out(data_out), .irq(irq), .ifr_set_ext(7'd0), .ifr_clr_ext(7'd0),
        .porta_out(porta_out), .porta_in(porta_in), .porta_bus(porta_bus),
        .ddra_out(ddra_out), .portb_out(portb_out), .portb_in(portb_in),
        .portb_bus(portb_bus), .pcr_out(pcr_out), .ddrb_out(ddrb_out),
        .ca1_in(ca1_in), .ca2_in(1'b1), .ca2_out(ca2_out),
        .cb1_in(cb1_in), .cb1_out(cb1_out), .cb2_in(cb2_in), .cb2_out(cb2_out)
    );

    task settle;
        repeat (3) @(posedge clk);
        #1;
    endtask
    task write_reg(input [3:0] a, input [7:0] d);
        @(negedge clk); addr = a; data_in = d; we = 1; strobe = 1;
        @(negedge clk); strobe = 0; we = 0;
        settle();
    endtask
    task tick;
        @(negedge clk); slow_clock = 1;
        @(negedge clk); slow_clock = 0;
        settle();
    endtask
    task check_bit(input actual, input expected, input [255:0] label);
        if (actual !== expected) $fatal(1, "%0s got=%b expected=%b", label, actual, expected);
    endtask
    task check_reg(input [3:0] a, input [7:0] expected, input [7:0] mask);
        addr = a; #1;
        if ((data_out & mask) !== (expected & mask))
            $fatal(1, "VIA R%h got=%h expected=%h mask=%h", a, data_out, expected, mask);
    endtask
    task reset_chip;
        @(negedge clk); reset = 1; power_reset = 1;
        settle();
        @(negedge clk); reset = 0; power_reset = 0;
        settle();
    endtask
    task check_phi2_shift(input [7:0] pattern);
        write_reg(4'ha, pattern);
        check_bit(cb1_out, 1, "SR initial CB1");
        for (integer b = 7; b >= 0; b = b - 1) begin
            tick();
            check_bit(cb1_out, 0, "SR falling CB1");
            check_bit(cb2_out, pattern[b], "SR MSB first");
            check_reg(4'hd, 0, 8'h04);
            tick();
            check_bit(cb1_out, 1, "SR rising CB1");
            check_bit(cb2_out, pattern[b], "SR full bit hold");
        end
        check_reg(4'hd, 8'h04, 8'h04);
        repeat (8) begin
            tick();
            check_bit(cb1_out, 1, "SR stops after eight");
            check_bit(cb2_out, pattern[0], "SR final bit hold");
        end
    endtask

    initial begin
        reset_chip();
        // DDR input pins feed PSG reads; output bits drive the DAC bus.
        write_reg(4'h3, 8'hf0);
        write_reg(4'h1, 8'h3c);
        if (porta_bus !== 8'h35 || ddra_out !== 8'hf0) $fatal(1, "VIA DDR/port A resolution");

        // PCR direct CA2 output is Vectrex's integrator zero control.
        write_reg(4'hc, 8'hcc);
        check_bit(ca2_out, 0, "CA2 direct zero");
        write_reg(4'hc, 8'hce);
        check_bit(ca2_out, 1, "CA2 direct release");
        write_reg(4'hc, 8'hca);
        write_reg(4'h1, 8'h80);
        check_bit(ca2_out, 0, "CA2 pulse start");
        settle();
        check_bit(ca2_out, 0, "CA2 pulse waits PHI2");
        tick();
        check_bit(ca2_out, 1, "CA2 pulse end");

        // N=3 reaches interrupt/PB7 completion on tick N+2.
        write_reg(4'h2, 8'h80);
        write_reg(4'hb, 8'h80);
        write_reg(4'he, 8'hc0);
        write_reg(4'h4, 8'h03);
        write_reg(4'h5, 8'h00);
        check_bit(portb_bus[7], 0, "T1 one-shot load");
        for (integer t = 0; t < 4; t = t + 1) begin
            tick();
            check_bit(portb_bus[7], 0, "T1 one-shot length");
            check_reg(4'hd, 0, 8'h40);
        end
        tick();
        check_bit(portb_bus[7], 1, "T1 one-shot complete");
        check_bit(irq, 1, "T1 IRQ asserted");
        // The one-shot must not reload/toggle on its short initial period.
        repeat (20) begin tick(); check_bit(portb_bus[7], 1, "T1 one-shot hold"); end
        write_reg(4'hd, 8'h40);
        check_bit(irq, 0, "T1 IRQ cleared");
        repeat (8) tick();
        check_reg(4'hd, 0, 8'h40);

        write_reg(4'hb, 8'hc0);
        write_reg(4'h4, 8'h02);
        write_reg(4'h5, 8'h00);
        for (integer period = 0; period < 4; period = period + 1) begin
            repeat (3) begin tick(); check_bit(portb_bus[7], period[0], "T1 free-run period"); end
            tick();
            check_bit(portb_bus[7], !period[0], "T1 free-run toggle");
            check_reg(4'hd, 8'h40, 8'h40);
            write_reg(4'hd, 8'h40);
        end

        reset_chip();
        write_reg(4'hb, 8'h18); // shift out on PHI2, no T1 PB7
        check_phi2_shift(8'h96); // different first/last bits expose rotation errors
        check_phi2_shift(8'h69);

        // PHI2 shifting must leave Timer 2 as an independent interval timer.
        write_reg(4'h8, 8'h03);
        write_reg(4'h9, 8'h00);
        repeat (4) begin tick(); check_reg(4'hd, 0, 8'h20); end
        tick();
        check_reg(4'hd, 8'h20, 8'h20);
        repeat (3) tick();
        check_reg(4'h8, 8'hfb, 8'hff);
        check_reg(4'h9, 8'hff, 8'hff);

        // T2-paced one-shot launches all eight bits and holds the final bit.
        reset_chip();
        write_reg(4'hb, 8'h14);
        write_reg(4'h8, 8'h01);
        write_reg(4'h9, 8'h00);
        write_reg(4'ha, 8'h53);
        begin : t2_one_shot
            integer bits_seen, cycles;
            reg old_cb1;
            reg [7:0] pattern;
            bits_seen = 0; cycles = 0; old_cb1 = cb1_out; pattern = 8'h53;
            while (bits_seen < 8 && cycles < 100) begin
                tick(); cycles = cycles + 1;
                if (old_cb1 && !cb1_out) begin
                    check_bit(cb2_out, pattern[7 - bits_seen], "T2 SR one-shot bits");
                    bits_seen = bits_seen + 1;
                end
                old_cb1 = cb1_out;
            end
            if (bits_seen != 8) $fatal(1, "T2 SR one-shot timeout");
            repeat (20) tick();
            check_bit(cb1_out, 1, "T2 SR one-shot clock stop");
            check_bit(cb2_out, 1, "T2 SR one-shot last hold");
            check_reg(4'hd, 8'h04, 8'h04);
        end

        // External-clock mode continues after the eighth pulse; each group
        // sets IFR2. The clock is synchronous to clk in this isolated test.
        reset_chip();
        write_reg(4'hb, 8'h1c);
        write_reg(4'ha, 8'hc5);
        begin : external_shift
            reg [7:0] pattern;
            pattern = 8'hc5;
            for (integer b = 0; b < 16; b = b + 1) begin
                @(negedge clk); cb1_in = 0; settle();
                check_bit(cb2_out, pattern[7 - (b % 8)], "external SR repeated bits");
                @(negedge clk); cb1_in = 1; settle();
                if (b == 7 || b == 15) begin
                    check_reg(4'hd, 8'h04, 8'h04);
                    write_reg(4'hd, 8'h04);
                end else check_reg(4'hd, 0, 8'h04);
            end
        end

        // PHI2 input receives eight samples and then stops its clock.
        reset_chip();
        write_reg(4'hb, 8'h08);
        write_reg(4'ha, 0);
        begin : input_shift
            reg [7:0] pattern;
            pattern = 8'h9a;
            for (integer b = 7; b >= 0; b = b - 1) begin
                cb2_in = pattern[b];
                tick(); tick();
            end
            check_reg(4'ha, pattern, 8'hff);
            check_reg(4'hd, 8'h04, 8'h04);
            repeat (4) begin tick(); check_bit(cb1_out, 1, "input SR stops after eight"); end
        end

        // T2-paced free-run repeats eight-bit data and never raises IFR2.
        reset_chip();
        write_reg(4'hb, 8'h10);
        write_reg(4'h8, 8'h00);
        write_reg(4'h9, 8'h00);
        write_reg(4'ha, 8'ha6);
        begin : free_run
            integer bits_seen, cycles;
            reg old_cb1;
            reg [7:0] pattern;
            bits_seen = 0; cycles = 0; old_cb1 = cb1_out; pattern = 8'ha6;
            while (bits_seen < 24 && cycles < 200) begin
                tick(); cycles = cycles + 1;
                if (old_cb1 && !cb1_out) begin
                    check_bit(cb2_out, pattern[7 - (bits_seen % 8)], "T2 SR repeated bits");
                    bits_seen = bits_seen + 1;
                end
                old_cb1 = cb1_out;
                check_reg(4'hd, 0, 8'h04);
            end
            if (bits_seen != 24) $fatal(1, "T2 SR free-run did not complete three bytes");
        end
        $display("PASS: Vectrex VIA timers, PB7, CA2, DDR and serial bit timing");
        $finish;
    end
    initial begin #2000000; $fatal(1, "VIA timeout"); end
endmodule

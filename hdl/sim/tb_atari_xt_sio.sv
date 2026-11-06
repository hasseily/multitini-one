// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps

module tb_atari_xt_sio;
    reg clk = 0;
    always #500 clk = !clk;

    reg reset = 1, machine_reset = 0, paused = 0;
    reg pia_we = 0, pokey_we = 0, pokey_re = 0;
    reg [7:0] pia_wdata = 0, pokey_addr = 0, pokey_wdata = 0;
    wire command_n, tx_valid, rx_mode, rx_read;
    wire rx_valid, rx_overrun, rx_busy;
    wire [7:0] tx_data, rx_data, pokey_data, skctl;
    reg event_ready = 1, reply_valid = 0;
    reg [16:0] reply_data = 0;
    wire event_valid, reply_ready;
    wire [17:0] event_data;

    multitini_atari_xt_pia pia (
        .clk(clk), .rst(reset || machine_reset), .we(pia_we), .waddr(16'hd303),
        .wdata(pia_wdata), .raddr(16'hd303), .rdata(),
        .joy_porta_in(8'hff), .joy_portb_in(8'hff), .joy_porta_out(), .joy_porta_oe(),
        .joy_portb_out(), .joy_portb_oe(), .portb_out_q(), .pia_irq_n(), .cb2_line(command_n)
    );
    assign rx_mode = skctl[4] && (|skctl[1:0]);
    assign rx_read = pokey_re && pokey_addr[3:0] == 4'hd;
    multitini_atari_xt_pokey #(.CLK_BUS_HZ(1_000_000)) pokey (
        .clk(clk), .rst(reset || machine_reset), .cold_boot(1'b0), .phi2_tick(1'b1),
        .we(pokey_we), .waddr(pokey_addr), .wdata(pokey_wdata),
        .re(pokey_re), .re_addr(pokey_addr), .raddr(pokey_addr), .rdata(pokey_data),
        .kbd_event_valid(1'b0), .kbd_event_code(8'd0), .kbd_release(1'b0),
        .shadow_pot0(8'd228), .shadow_pot1(8'd228), .shadow_pot2(8'd228), .shadow_pot3(8'd228),
        .shadow_pot4(8'd228), .shadow_pot5(8'd228), .shadow_pot6(8'd228), .shadow_pot7(8'd228),
        .shadow_allpot(8'd0), .bridge_potgo_pulse(), .bridge_fast_scan(),
        .ch1_out(), .ch2_out(), .ch3_out(), .ch4_out(),
        .ser_out_complete(1'b1), .ser_out_ready_pulse(1'b0),
        .ser_in_byte_pulse(rx_valid), .ser_in_byte(rx_data), .break_key_pulse(1'b0),
        .ser_framing_err(1'b0), .ser_input_overrun(rx_overrun), .ser_input_busy(rx_busy),
        .irq_n(), .serout_byte(), .serout_strobe(), .skctl_out(skctl),
        .tx_complete_byte(tx_data), .tx_complete_valid(tx_valid)
    );
    multitini_atari_xt_sio #(.CLOCK_HZ(1_000_000)) dut (
        .clk(clk), .reset(reset), .machine_reset(machine_reset), .paused(paused),
        .command_n(command_n), .tx_valid(tx_valid), .tx_data(tx_data),
        .rx_mode(rx_mode), .rx_read(rx_read), .rx_valid(rx_valid), .rx_overrun(rx_overrun),
        .rx_busy(rx_busy), .rx_data(rx_data), .event_valid(event_valid),
        .event_data(event_data), .event_ready(event_ready), .reply_valid(reply_valid),
        .reply_data(reply_data), .reply_ready(reply_ready)
    );

    reg [17:0] captured [0:15];
    reg [7:0] received [0:7];
    time arrival [0:7];
    integer event_count = 0, receive_count = 0, overruns = 0;
    always @(posedge clk) begin
        if (event_valid && event_ready) begin
            captured[event_count] = event_data;
            event_count++;
        end
        if (rx_valid) begin
            received[receive_count] = rx_data;
            arrival[receive_count] = $time;
            receive_count++;
        end
        if (rx_overrun) overruns++;
    end

    task automatic command(input bit asserted);
        @(negedge clk);
        pia_wdata = asserted ? 8'h34 : 8'h3c;
        pia_we = 1;
        @(negedge clk);
        pia_we = 0;
    endtask

    task automatic write_pokey(input [7:0] address, data);
        @(negedge clk);
        pokey_addr = address;
        pokey_wdata = data;
        pokey_we = 1;
        @(negedge clk);
        pokey_we = 0;
    endtask

    task automatic read_pokey(input [7:0] address, data, mask);
        @(negedge clk);
        pokey_addr = address;
        pokey_re = 1;
        @(posedge clk);
        if ((pokey_data & mask) !== (data & mask))
            $fatal(1, "POKEY $%h read %h instead of %h under mask %h", address, pokey_data, data, mask);
        @(negedge clk);
        pokey_re = 0;
    endtask

    task automatic reply(input bit gap, input [7:0] epoch, data);
        @(negedge clk);
        reply_data = {gap, epoch, data};
        reply_valid = 1;
        do @(posedge clk); while (!reply_ready);
        @(negedge clk);
        reply_valid = 0;
    endtask

    task automatic expect_event(input integer index, input [7:0] epoch, input [1:0] kind, input [7:0] data);
        do @(negedge clk); while (event_count <= index);
        if (captured[index] !== {epoch, kind, data})
            $fatal(1, "SIO event %d was %h expected %h", index, captured[index], {epoch, kind, data});
    endtask

    task automatic expect_byte(input integer index, input [7:0] value);
        do @(negedge clk); while (receive_count <= index);
        if (received[index] !== value) $fatal(1, "SIO byte %d was %h expected %h", index, received[index], value);
    endtask

    initial begin
        repeat (4) @(negedge clk);
        reset = 0;
        command(1);
        expect_event(0, 8'd1, 2'd1, 0);
        write_pokey(8'h06, 0); // AUDF4
        write_pokey(8'h0f, 8'h23); // timer 4 TX, no asynchronous RX
        write_pokey(8'h0d, 8'h31); // Drive 1 byte
        repeat (100) @(negedge clk);
        if (event_count != 1) $fatal(1, "SEROUT escaped before it finished shifting");
        expect_event(1, 8'd1, 2'd0, 8'h31);
        command(0);
        expect_event(2, 8'd1, 2'd2, 0);

        reply(0, 8'd0, 8'hff); // Stale bytes must drain without blocking the queue.
        @(negedge clk);
        reply_data = {1'b0, 8'd1, 8'h41};
        reply_valid = 1;
        repeat (600) @(negedge clk);
        if (reply_ready || rx_busy || receive_count != 0) $fatal(1, "RX accepted a byte in transmit mode");
        write_pokey(8'h0e, 8'h20); // IRQEN serial input
        write_pokey(8'h0f, 8'h13); // asynchronous RX
        wait (rx_busy);
        @(negedge clk);
        reply_valid = 0;
        expect_byte(0, 8'h41);
        read_pokey(8'h0e, 8'h00, 8'h20); // IRQST bit 5 cleared
        read_pokey(8'h0d, 8'h41, 8'hff);

        reply(0, 8'd1, 8'h43);
        expect_byte(1, 8'h43);
        if (arrival[1] - arrival[0] < 550000 || arrival[1] - arrival[0] > 553000)
            $fatal(1, "Normal receive spacing %0t", arrival[1] - arrival[0]);
        reply(1, 8'd1, 8'hb7);
        expect_byte(2, 8'hb7);
        if (arrival[2] - arrival[1] < 1000000 || arrival[2] - arrival[1] > 1003000 || overruns != 1)
            $fatal(1, "Long gap or receive overrun incorrect");
        read_pokey(8'h0d, 8'hb7, 8'hff);

        reply(0, 8'd1, 8'hb8);
        paused = 1;
        repeat (600) @(negedge clk);
        if (receive_count != 3 || !rx_busy) $fatal(1, "Pause did not preserve the in-flight byte");
        paused = 0;
        expect_byte(3, 8'hb8);
        read_pokey(8'h0d, 8'hb8, 8'hff);

        command(1);
        expect_event(3, 8'd2, 2'd1, 0);
        reply(0, 8'd1, 8'hff); // The just-replaced transaction.
        event_ready = 0;
        command(0);
        command(1);
        command(0); // Third event overflows the two-slot staging queue.
        repeat (20) @(negedge clk);
        if (!event_valid || event_data !== {8'd2, 2'd2, 8'd0} || receive_count != 4)
            $fatal(1, "Offered event changed under backpressure");
        event_ready = 1;
        expect_event(4, 8'd2, 2'd2, 0);
        expect_event(5, 8'd3, 2'd1, 0);
        expect_event(6, 8'd3, 2'd3, 0);
        reply(0, 8'd3, 8'hff); // Overflow poisoned this exchange.

        @(negedge clk);
        machine_reset = 1;
        repeat (4) @(negedge clk);
        machine_reset = 0;
        command(1);
        expect_event(7, 8'd4, 2'd1, 0);
        command(0);
        expect_event(8, 8'd4, 2'd2, 0);
        if (receive_count != 4) $fatal(1, "Stale byte survived warm reset");
        $display("PASS Atari-XT standard-speed PIA and POKEY SIO transport");
        $finish;
    end

    initial begin
        #15000000;
        $fatal(1, "Atari-XT SIO test timeout");
    end
endmodule

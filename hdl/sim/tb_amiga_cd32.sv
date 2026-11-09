// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_amiga_cd32;
    localparam [8:1] POTGO = 8'h1a, POTINP = 8'h0b;
    reg clk = 0;
    always #5 clk = !clk;
    reg [1:0] phase = 0;
    always @(posedge clk) phase <= phase + 1'b1;
    reg reset = 1;
    reg [8:1] address = POTINP;
    reg [15:0] data_in = 0;
    wire [15:0] data_out;
    reg [1:0] cia_fire_out = 2'b11;
    wire [1:0] cia_fire_in;
    reg [15:0] primary = 16'hffff, secondary = 16'hffff;
    reg [2:0] mouse = 0;
    reg uio_en = 0, uio_strobe = 0;
    reg [15:0] uio_data = 0;

    userio dut (
        .clk(clk), .reset(reset), .clk7_en(phase == 0),
        .reg_address_in(address), .data_in(data_in), .data_out(data_out), .pot_cnt_en(1'b0),
        ._fire0(cia_fire_in[0]), ._fire1(cia_fire_in[1]),
        ._fire0_dat(cia_fire_out[0]), ._fire1_dat(cia_fire_out[1]),
        ._joy1(primary), ._joy2(secondary), .joy_ana1(16'd0), .joy_ana2(16'd0),
        .mouse_btn(mouse), .kms_level(1'b0), .kbd_mouse_type(2'd0), .kbd_mouse_data(8'd0),
        .IO_ENA(uio_en), .IO_STROBE(uio_strobe), .IO_DIN(uio_data), .host_rdat(16'd0), .host_ack(1'b0)
    );

    task automatic settle;
        repeat (8) @(negedge clk);
    endtask

    task automatic set_mode(input bit cd32);
        uio_en = 1;
        uio_data = 16'h00f9;
        uio_strobe = 1;
        @(negedge clk);
        uio_strobe = 0;
        @(negedge clk);
        uio_data = cd32 ? 16'h0004 : 16'd0;
        uio_strobe = 1;
        @(negedge clk);
        uio_strobe = 0;
        uio_en = 0;
        settle();
    endtask

    task automatic set_potgo(input [15:0] value);
        address = POTGO;
        data_in = value;
        settle();
        address = POTINP;
        settle();
    endtask

    task automatic direct_inputs;
        set_potgo(16'd0);
        primary[4] = 0;
        primary[5] = 0;
        secondary[4] = 0;
        secondary[5] = 0;
        settle();
        if (cia_fire_in !== 2'b00 || data_out[14] !== 0 || data_out[10] !== 0)
            $fatal(1, "Direct joystick fire inputs changed");
        primary = 16'hffff;
        secondary = 16'hffff;
        mouse = 3'b111;
        settle();
        if (cia_fire_in !== 2'b10 || data_out[10] !== 0 || data_out[8] !== 0)
            $fatal(1, "Direct mouse button inputs changed");
        mouse = 0;
        settle();
        if (cia_fire_in !== 2'b11 || data_out[10] !== 1 || data_out[8] !== 1)
            $fatal(1, "Released mouse buttons did not clear");
    endtask

    task automatic serial_poll(input integer port, input [3:0] pressed);
        reg [8:0] expected;
        // First four samples are Blue, Red, Yellow, Green. The three
        // unmapped pad buttons are released, followed by the 1,0 signature.
        expected = {~pressed[1], ~pressed[0], ~pressed[2], ~pressed[3], 4'b1111, 1'b0};
        set_potgo(16'd0);
        if (port == 1) primary = {8'hff, ~pressed, 4'hf};
        else secondary = {8'hff, ~pressed, 4'hf};
        settle();
        set_potgo(port == 1 ? 16'h2000 : 16'h0200);
        for (int i = 8; i >= 0; i--) begin
            if (data_out[port == 1 ? 14 : 10] !== expected[i])
                $fatal(1, "CD32 port %0d buttons %b sample %0d expected %b got %b",
                       port + 1, pressed, 8 - i, expected[i], data_out[port == 1 ? 14 : 10]);
            cia_fire_out[port] = 0;
            settle();
            cia_fire_out[port] = 1;
            settle();
        end
        set_potgo(16'd0);
    endtask

    initial begin
        settle();
        reset = 0;
        set_mode(0);
        direct_inputs();
        // Digital mode keeps Blue on POTINP even while the guest toggles pin 5.
        primary[5] = 0;
        set_potgo(16'h2000);
        if (data_out[14] !== 0 || cia_fire_in[1] !== 1)
            $fatal(1, "CD32 behavior enabled in digital mode");
        primary = 16'hffff;

        set_mode(1);
        direct_inputs();
        // The shared port selects its joystick when Red is first pressed.
        secondary[4] = 0;
        settle();
        for (int port = 0; port < 2; port++) begin
            for (int button = 0; button < 4; button++)
                serial_poll(port, 4'b0001 << button);
            serial_poll(port, 4'b0000);
        end
        direct_inputs();
        $display("PASS Amiga CD32 serial buttons, direct fire and mouse inputs");
        $finish;
    end

    initial begin
        #100000;
        $fatal(1, "CD32 input watchdog");
    end
endmodule

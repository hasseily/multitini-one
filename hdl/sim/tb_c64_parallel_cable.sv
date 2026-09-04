// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// Opt-in DolphinDOS-style parallel cable of the 1541 wrapper, exercised with
// the imported drive CPU and VIAs. The T65 and 6522 models are VHDL, so this
// test needs a mixed-language simulator; scripts/run_xsim_tests.sh runs it
// under XSim. A synthetic 16 KiB drive ROM drives VIA1 port A and CA2 and
// polls CB1 the way a parallel fast loader does, probes the 8 KiB extension
// RAM at $8000-$9FFF through the same port, and finally drives CA2 in manual
// output mode. The C64 side is outside this bench: the adapter feeds the
// same strobe to FLAG2 and PA2, so the wrapper-level level check covers it.
module tb_c64_parallel_cable;
    logic clk = 1'b0;
    logic reset = 1'b1;
    logic soft_reset = 1'b0;
    logic drive_rom_write = 1'b0;
    logic [13:0] drive_rom_address = 14'd0;
    logic [7:0] drive_rom_write_data = 8'd0;
    logic drive_rom_loaded = 1'b0;
    logic parallel_enable = 1'b0;
    logic par_stb_in = 1'b1;
    logic [7:0] par_data_out;
    logic par_stb_out;

    logic [7:0] rom [0:16383];
    int strobe_pulses = 0;
    logic [7:0] strobed_data = 8'h00;

    always #5 clk = !clk;

    multitini_c64_1541 dut (
        .clk(clk),
        .reset(reset),
        .soft_reset(soft_reset),
        .clear_error(1'b0),
        .drive_rom_write(drive_rom_write),
        .drive_rom_address(drive_rom_address),
        .drive_rom_write_data(drive_rom_write_data),
        .drive_rom_loaded(drive_rom_loaded),
        .c64_iec_atn(1'b1),
        .c64_iec_data(1'b1),
        .c64_iec_clk(1'b1),
        .parallel_enable(parallel_enable),
        .par_data_in(8'hFF),
        .par_stb_in(par_stb_in),
        .par_data_out(par_data_out),
        .par_stb_out(par_stb_out),
        .media_mount(1'b0),
        .media_eject(1'b0),
        .media_complete(1'b0),
        .media_slot(8'd0),
        .media_kind(8'd0),
        .media_read_only(1'b1),
        .media_image_size(32'd0),
        .media_generation(32'd0),
        .media_complete_request_id(32'd0),
        .media_complete_status(32'd0),
        .media_complete_block_count(16'd0),
        .media_complete_generation(32'd0),
        .media_write_data_ready(1'b1),
        .d64_load_data(8'd0),
        .d64_load_valid(1'b0),
        .d64_load_first(1'b0),
        .d64_load_last(1'b0),
        .d64_load_offset(32'd0),
        .d64_load_total(32'd0),
        .g64_load_data(8'd0),
        .g64_load_valid(1'b0),
        .g64_load_first(1'b0),
        .g64_load_last(1'b0),
        .g64_load_offset(32'd0),
        .g64_load_total(32'd0)
    );

    // Count active-low strobes and capture the byte presented when each ends,
    // which is what a C64 reading $DD01 after its FLAG interrupt would see.
    always @(negedge par_stb_out) strobe_pulses++;
    always @(posedge par_stb_out) strobed_data <= par_data_out;

    task automatic check_value(input logic condition, input string message);
        if (!condition) begin
            $display("FAIL: %s", message);
            $fatal(1);
        end
    endtask

    // The ROM occupies $C000-$FFFF, so offset $1000 is also what the drive
    // reads at $9000 when the extension RAM is not mapped.
    task automatic build_rom;
        int index;
        for (index = 0; index < 16384; index++) rom[index] = 8'hEA;
        rom['h1000] = 8'h3C;
        // $C000  LDA #$FF : STA $1803        DDRA: port A drives the cable
        rom['h0000] = 8'hA9; rom['h0001] = 8'hFF;
        rom['h0002] = 8'h8D; rom['h0003] = 8'h03; rom['h0004] = 8'h18;
        // $C005  LDA #$0B : STA $180C        PCR: CA2 pulse out, CA1 rising
        rom['h0005] = 8'hA9; rom['h0006] = 8'h0B;
        rom['h0007] = 8'h8D; rom['h0008] = 8'h0C; rom['h0009] = 8'h18;
        // $C00A  LDA #$C3 : STA $9000        marker into the extension RAM
        rom['h000A] = 8'hA9; rom['h000B] = 8'hC3;
        rom['h000C] = 8'h8D; rom['h000D] = 8'h00; rom['h000E] = 8'h90;
        // $C00F  LDA $9000 : STA $00         keep what came back
        rom['h000F] = 8'hAD; rom['h0010] = 8'h00; rom['h0011] = 8'h90;
        rom['h0012] = 8'h85; rom['h0013] = 8'h00;
        // $C014  LDA #$A5 : STA $1801        first byte plus CA2 strobe
        rom['h0014] = 8'hA9; rom['h0015] = 8'hA5;
        rom['h0016] = 8'h8D; rom['h0017] = 8'h01; rom['h0018] = 8'h18;
        // $C019  LDA $180D : AND #$10 : BEQ $C019   wait for CB1
        rom['h0019] = 8'hAD; rom['h001A] = 8'h0D; rom['h001B] = 8'h18;
        rom['h001C] = 8'h29; rom['h001D] = 8'h10;
        rom['h001E] = 8'hF0; rom['h001F] = 8'hF9;
        // $C020  LDA $1800                   clear the CB1 flag
        rom['h0020] = 8'hAD; rom['h0021] = 8'h00; rom['h0022] = 8'h18;
        // $C023  LDA #$5A : STA $1801        second byte plus strobe
        rom['h0023] = 8'hA9; rom['h0024] = 8'h5A;
        rom['h0025] = 8'h8D; rom['h0026] = 8'h01; rom['h0027] = 8'h18;
        // $C028  LDA $180D : AND #$10 : BEQ $C028   wait for CB1
        rom['h0028] = 8'hAD; rom['h0029] = 8'h0D; rom['h002A] = 8'h18;
        rom['h002B] = 8'h29; rom['h002C] = 8'h10;
        rom['h002D] = 8'hF0; rom['h002E] = 8'hF9;
        // $C02F  LDA $1800                   clear the CB1 flag
        rom['h002F] = 8'hAD; rom['h0030] = 8'h00; rom['h0031] = 8'h18;
        // $C032  LDA $00 : STA $1801         report the RAM readback
        rom['h0032] = 8'hA5; rom['h0033] = 8'h00;
        rom['h0034] = 8'h8D; rom['h0035] = 8'h01; rom['h0036] = 8'h18;
        // $C037  LDA #$0D : STA $180C        CA2 manual low
        rom['h0037] = 8'hA9; rom['h0038] = 8'h0D;
        rom['h0039] = 8'h8D; rom['h003A] = 8'h0C; rom['h003B] = 8'h18;
        // $C03C  LDA $180D : AND #$10 : BEQ $C03C   wait for CB1
        rom['h003C] = 8'hAD; rom['h003D] = 8'h0D; rom['h003E] = 8'h18;
        rom['h003F] = 8'h29; rom['h0040] = 8'h10;
        rom['h0041] = 8'hF0; rom['h0042] = 8'hF9;
        // $C043  LDA $1800                   clear the CB1 flag
        rom['h0043] = 8'hAD; rom['h0044] = 8'h00; rom['h0045] = 8'h18;
        // $C046  LDA #$0F : STA $180C        CA2 manual high
        rom['h0046] = 8'hA9; rom['h0047] = 8'h0F;
        rom['h0048] = 8'h8D; rom['h0049] = 8'h0C; rom['h004A] = 8'h18;
        // $C04B  JMP $C04B
        rom['h004B] = 8'h4C; rom['h004C] = 8'h4B; rom['h004D] = 8'hC0;
        // NMI, reset, and IRQ vectors.
        rom['h3FFA] = 8'h4B; rom['h3FFB] = 8'hC0;
        rom['h3FFC] = 8'h00; rom['h3FFD] = 8'hC0;
        rom['h3FFE] = 8'h4B; rom['h3FFF] = 8'hC0;
    endtask

    task automatic load_rom;
        int index;
        for (index = 0; index < 16384; index++) begin
            @(negedge clk);
            drive_rom_write = 1'b1;
            drive_rom_address = index[13:0];
            drive_rom_write_data = rom[index];
        end
        @(negedge clk);
        drive_rom_write = 1'b0;
    endtask

    task automatic wait_until_data(input logic [7:0] expected,
                                   input string message);
        int cycles = 0;
        while (par_data_out !== expected) begin
            @(posedge clk); #1;
            cycles++;
            if (cycles > 200000) begin
                $display("FAIL: %s (par_data_out=%02x)", message,
                         par_data_out);
                $fatal(1);
            end
        end
    endtask

    task automatic wait_until_strobes(input int expected,
                                      input string message);
        int cycles = 0;
        while (strobe_pulses != expected || !par_stb_out) begin
            @(posedge clk); #1;
            cycles++;
            if (cycles > 200000) begin
                $display("FAIL: %s (strobes=%0d)", message, strobe_pulses);
                $fatal(1);
            end
        end
    endtask

    task automatic wait_until_strobe_level(input logic expected,
                                           input string message);
        int cycles = 0;
        while (par_stb_out !== expected) begin
            @(posedge clk); #1;
            cycles++;
            if (cycles > 200000) begin
                $display("FAIL: %s", message);
                $fatal(1);
            end
        end
    endtask

    task automatic wait_until_rom_drives(input logic [7:0] expected,
                                         input string message);
        int cycles = 0;
        while (dut.drive_logic.uc1_pa_oe !== 8'hFF ||
               dut.drive_logic.uc1_pa_o !== expected) begin
            @(posedge clk); #1;
            cycles++;
            if (cycles > 200000) begin
                $display("FAIL: %s", message);
                $fatal(1);
            end
        end
    endtask

    // Hold the C64-side strobe low for longer than one 1 MHz drive cycle.
    task automatic strobe_drive;
        @(negedge clk);
        par_stb_in = 1'b0;
        repeat (40) @(posedge clk);
        @(negedge clk);
        par_stb_in = 1'b1;
    endtask

    initial begin
        build_rom();
        repeat (4) @(posedge clk);
        @(negedge clk);
        reset = 1'b0;
        load_rom();
        repeat (4) @(posedge clk);
        @(negedge clk);
        drive_rom_loaded = 1'b1;

        // (a) Cable disabled: the ROM drives port A, the C64 sees nothing.
        wait_until_rom_drives(8'hA5,
                              "drive ROM drives VIA1 port A with the cable off");
        repeat (100) @(posedge clk);
        #1;
        check_value(par_data_out == 8'hFF,
                    "disabled cable presents released data lines");
        check_value(par_stb_out,
                    "disabled cable presents a released strobe");
        check_value(strobe_pulses == 0,
                    "no CA2 pulse leaves the wrapper while disabled");
        check_value(dut.drive_logic.ram.ram[0] == 8'h3C,
                    "$9000 reads ROM, not RAM, while the cable is off");

        // (b) Enable the cable across a drive reset, as the firmware does.
        @(negedge clk);
        soft_reset = 1'b1;
        parallel_enable = 1'b1;
        repeat (8) @(posedge clk);
        #1;
        check_value(par_data_out == 8'hFF && par_stb_out,
                    "cable lines idle high while the drive is in reset");
        strobe_pulses = 0;
        @(negedge clk);
        soft_reset = 1'b0;
        wait_until_data(8'hA5, "first byte reaches the cable when enabled");
        wait_until_strobes(1, "STA $1801 pulses the strobe low once");
        check_value(strobed_data == 8'hA5,
                    "the strobe ends with the first byte presented");
        check_value(dut.drive_logic.ram.ram[0] == 8'hC3,
                    "$9000 reads back the RAM marker while the cable is on");

        // (c) A C64-side strobe is seen on CB1 and answered with the next byte.
        strobe_drive();
        wait_until_data(8'h5A, "second byte follows the C64 strobe");
        wait_until_strobes(2, "second byte carries its own strobe");
        check_value(strobed_data == 8'h5A,
                    "the second strobe ends with the second byte presented");

        // (d) The RAM readback is reported over the cable after one more
        // strobe: the marker proves $8000-$9FFF is writable and readable.
        strobe_drive();
        wait_until_data(8'hC3, "RAM marker is reported over the cable");
        wait_until_strobes(3, "RAM report carries its own strobe");

        // (e) CA2 manual output mode (PCR $0D low, $0F high) reaches the
        // cable as a held level, which the C64 reads on PA2, not as a pulse.
        wait_until_strobe_level(1'b0,
                                "manual CA2 low reaches the cable");
        repeat (200) @(posedge clk);
        #1;
        check_value(!par_stb_out, "manual CA2 low is held, not pulsed");
        check_value(strobe_pulses == 4,
                    "manual CA2 low is one falling edge");
        strobe_drive();
        wait_until_strobe_level(1'b1,
                                "manual CA2 high releases the cable");
        repeat (200) @(posedge clk);
        #1;
        check_value(par_stb_out, "manual CA2 high is held");
        check_value(strobe_pulses == 4,
                    "manual CA2 high adds no falling edge");

        // Disabling the cable releases the lines at once.
        @(negedge clk);
        parallel_enable = 1'b0;
        #1;
        check_value(par_data_out == 8'hFF && par_stb_out,
                    "disabling the cable releases the lines immediately");
        @(negedge clk);
        parallel_enable = 1'b1;
        #1;
        check_value(par_data_out == 8'hC3,
                    "re-enabling the cable presents the drive port again");

        $display("PASS: C64 1541 DolphinDOS-style parallel cable");
        $finish;
    end
endmodule

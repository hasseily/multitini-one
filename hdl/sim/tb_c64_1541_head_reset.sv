// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

module tb_c64_1541_head_reset;
    logic clk = 1'b0;
    logic reset = 1'b1;
    logic soft_reset = 1'b0;
    logic pause_request = 1'b0;
    logic drive_rom_write = 1'b0;
    logic drive_rom_loaded = 1'b1;

    always #5 clk = !clk;

    // The media and imported-drive leaves are black boxes in this focused
    // test. Force only the two physical drive signals consumed by the head
    // tracker so reset-phase sampling can be checked in isolation.
    multitini_c64_1541 dut (
        .clk(clk),
        .reset(reset),
        .soft_reset(soft_reset),
        .pause_request(pause_request),
        .clear_error(1'b0),
        .drive_rom_write(drive_rom_write),
        .drive_rom_address(14'd0),
        .drive_rom_write_data(8'd0),
        .drive_rom_loaded(drive_rom_loaded),
        .c64_iec_atn(1'b1),
        .c64_iec_data(1'b1),
        .c64_iec_clk(1'b1),
        .parallel_enable(1'b0),
        .par_data_in(8'hFF),
        .par_stb_in(1'b1),
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

    task automatic check_value(input logic condition, input string message);
        if (!condition) begin
            $display("FAIL: %s", message);
            $fatal(1);
        end
    endtask

    initial begin
        force dut.stepper_phase = 2'd3;
        force dut.motor_on = 1'b1;

        repeat (3) @(posedge clk);
        #1;
        check_value(dut.head_half_track == 7'd34,
                    "hard reset parks the head at track 18");

        @(negedge clk);
        reset = 1'b0;
        repeat (3) begin
            @(posedge clk); #1;
            check_value(dut.head_half_track == 7'd34,
                        "reset release does not synthesize a phase 0-to-3 step");
        end

        force dut.stepper_phase = 2'd0;
        @(posedge clk); #1;
        check_value(dut.head_half_track == 7'd35,
                    "first real forward phase advances one half-track");

        @(negedge clk);
        soft_reset = 1'b1;
        force dut.stepper_phase = 2'd3;
        @(posedge clk); #1;
        check_value(dut.head_half_track == 7'd34,
                    "soft reset reparks the head at track 18");
        @(negedge clk);
        soft_reset = 1'b0;
        repeat (2) begin
            @(posedge clk); #1;
            check_value(dut.head_half_track == 7'd34,
                        "soft-reset release suppresses the sampled phase edge");
        end

        force dut.stepper_phase = 2'd0;
        @(posedge clk); #1;
        check_value(dut.head_half_track == 7'd35,
                    "real motion remains enabled after soft reset");

        // Pause at each drive PH2 phase. The CE and PH2 output registers drain
        // for two clocks; then both the fractional 16 MHz accumulator and the
        // CPU/VIA divider must remain frozen without asserting reset.
        for (integer target = 0; target < 16; target++) begin
            logic [31:0] held_drive_phase;
            logic [3:0] held_phase_div;
            @(negedge clk);
            while (dut.phase_div != target[3:0]) @(negedge clk);
            pause_request = 1'b1;
            repeat (3) @(negedge clk);
            held_drive_phase = dut.drive_phase;
            held_phase_div = dut.phase_div;
            repeat (31) begin
                @(negedge clk);
                check_value(dut.drive_phase == held_drive_phase &&
                            dut.phase_div == held_phase_div,
                            "menu pause preserves drive clock phases");
                check_value(!dut.drive_ce && !dut.ph2_r && !dut.ph2_f,
                            "menu pause suppresses CPU VIA and GCR enables");
                check_value(!dut.drive_reset,
                            "menu pause preserves the running drive state");
            end
            pause_request = 1'b0;
            repeat (35) @(negedge clk);
            check_value(dut.drive_phase != held_drive_phase,
                        "drive resumes after the menu closes");
        end

        $display("PASS: C64 1541 head reset and menu pause at all PH2 phases");
        $finish;
    end
endmodule

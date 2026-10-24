// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

module tb_island_guard;
    logic clk = 1'b0;
    logic reset = 1'b1;
    logic turbo_request = 1'b0;
    logic c64_iec_atn = 1'b1;
    logic c64_iec_data = 1'b1;
    logic c64_iec_clk = 1'b1;
    logic drive_iec_data = 1'b1;
    logic drive_iec_clk = 1'b1;
    logic c64_par_stb = 1'b1;
    logic drive_par_stb = 1'b1;
    logic turbo_active;
    logic [1:0] holdoff_select=0;
    logic static_hold_release=0;
    wire [1:0] block_cause;
    integer checks=0,code,n;

    always #5 clk = !clk;

    multitini_c64_cpu_turbo_guard #(
        .CORE_CLOCK_HZ(32),
        .IEC_HOLDOFF_CYCLES(4),.IEC_HOLDOFF_100MS_CYCLES(3),.IEC_HOLDOFF_50MS_CYCLES(2),.IEC_HOLDOFF_20MS_CYCLES(1),.STATIC_HOLD_CYCLES(12)
    ) dut (.*);

    task automatic check_value(input logic condition, input string message);
        checks++;
        if (!condition) begin
            $display("FAIL: %s", message);
            $fatal(1);
        end
    endtask

    task automatic step;
        @(posedge clk);
        #1;
    endtask

    initial begin
        repeat (2) step();
        reset = 1'b0;
        step();
        check_value(!turbo_active, "turbo defaults off");

        turbo_request = 1'b1;
        #1;
        check_value(turbo_active,
                    "idle bus permits independently requested turbo");

        @(negedge clk);
        c64_iec_clk = 1'b0;
        #1;
        check_value(!turbo_active,
                    "an IEC edge immediately suspends CPU turbo");
        repeat (3) begin
            step();
            check_value(!turbo_active,
                        "an asserted IEC line keeps CPU turbo suspended");
        end

        c64_iec_clk = 1'b1;
        repeat (4) begin
            step();
            check_value(!turbo_active,
                        "CPU turbo observes the full post-IEC holdoff");
        end
        step();
        check_value(turbo_active,
                    "CPU turbo resumes after the idle holdoff");

        @(negedge clk);
        c64_par_stb = 1'b0;
        #1;
        check_value(!turbo_active,
                    "a C64 parallel-cable strobe suspends CPU turbo");
        step();
        c64_par_stb = 1'b1;
        repeat (4) begin
            step();
            check_value(!turbo_active,
                        "the cable strobe observes the same holdoff");
        end
        step();
        check_value(turbo_active,
                    "CPU turbo resumes after the cable strobe holdoff");

        @(negedge clk);
        drive_par_stb = 1'b0;
        #1;
        check_value(!turbo_active,
                    "a drive parallel-cable strobe suspends CPU turbo");
        step();
        drive_par_stb = 1'b1;
        repeat (5) step();
        check_value(turbo_active,
                    "CPU turbo resumes after the drive strobe holdoff");

        @(negedge clk);
        drive_iec_data = 1'b0;
        #1;
        check_value(!turbo_active,
                    "drive-side IEC activity also suspends CPU turbo");
        turbo_request = 1'b0;
        drive_iec_data = 1'b1;
        repeat (6) step();
        check_value(!turbo_active,
                    "IEC state cannot implicitly request CPU turbo");

        // Retain every original assertion above, then exercise all new codes.
        turbo_request=1;
        for(code=0;code<4;code++)begin
            @(negedge clk);holdoff_select=code;c64_iec_clk=0;
            #1;check_value(block_cause==1,"IEC held takes priority");step();
            @(negedge clk);c64_iec_clk=1;step();
            check_value(block_cause==3,"edge and holdoff cause");
            for(n=0;n<4-code;n++)begin
                check_value(!turbo_active,"full selected holdoff");step();
            end
            check_value(turbo_active && block_cause==0,"selected holdoff expired");
        end
        @(negedge clk);static_hold_release=1;c64_par_stb=0;step();
        check_value(block_cause==2,"parallel held cause");
        repeat(13)step();
        check_value(turbo_active && block_cause==0,"static parallel release masks level and reload");
        repeat(5)step();check_value(turbo_active,"static release stays released");
        @(negedge clk);c64_iec_data=0;#1;
        check_value(!turbo_active && block_cause!=0,"any new edge immediately retrips");step();
        check_value(block_cause==1,"IEC has priority over parallel");
        repeat(13)step();check_value(turbo_active,"static IEC release masks both paths");
        @(negedge clk);static_hold_release=0;#1;
        check_value(!turbo_active && block_cause==1,"disabling experiment restores held gate");
        $display("GUARD PASS checks=%0d",checks);
        $finish;
    end
endmodule

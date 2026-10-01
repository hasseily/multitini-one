`timescale 1ns / 1ps
module tb_display_reset;
    reg clk = 0, clock_running = 1, clock_resetn = 0, hold_request = 0;
    real half_period = 3.367;
    always begin
        #(half_period);
        if (clock_running) clk = !clk;
    end
    wire resetn;
    multitini_display_reset dut(.*);
    reg [2:0] expected_hold = 3'b111;
    realtime last_edge = -1;
    integer edges = 0, checks = 0;
    always @(posedge clk or negedge clock_resetn) begin
        if (!clock_resetn) expected_hold = 3'b111;
        else begin
            last_edge = $realtime;
            expected_hold = {expected_hold[1:0], hold_request};
            edges++;
        end
        #0.001;
        if (resetn !== !expected_hold[2])
            $fatal(1, "reset response differs from three pixel edges");
        checks++;
    end
    always @(resetn) if ($time > 0 && clock_resetn && $realtime != last_edge)
        $fatal(1, "hold/release changed reset between pixel edges");
    task automatic settle;
        repeat (4) @(negedge clk);
    endtask
    real mode_half_period[6];
    integer scenarios = 0;
    initial begin
        mode_half_period[0]=3.367; mode_half_period[1]=7.692;
        mode_half_period[2]=7.407; mode_half_period[3]=4.630;
        mode_half_period[4]=5.848; mode_half_period[5]=4.202;
        settle();
        for (int mode = 0; mode < 6; mode++) begin
            half_period = mode_half_period[mode];
            for (int phase = 0; phase < 8; phase++) begin
                clock_resetn=0; hold_request=0;
                settle();
                #(half_period * (phase+0.25)/8.0);
                clock_resetn=1;
                settle();
                if (!resetn) $fatal(1, "initial release did not complete");
                #(half_period * (phase+0.75)/8.0);
                hold_request=1;
                settle();
                if (resetn) $fatal(1, "registered hold did not assert reset");
                #(half_period * (phase+0.25)/8.0);
                hold_request=0;
                settle();
                if (!resetn) $fatal(1, "hold release did not complete");
                // Loss of the MMCM clock must assert reset without any edge.
                @(negedge clk); clock_running=0;
                #(half_period/3.0); clock_resetn=0;
                #0.002;
                if (resetn) $fatal(1, "clock loss did not assert immediately");
                hold_request=1;
                #(4*half_period); clock_resetn=1;
                #(4*half_period);
                if (resetn) $fatal(1, "reset released while pixel clock stopped");
                clock_running=1;
                settle();
                if (resetn) $fatal(1, "relock briefly released an active hold");
                hold_request=0;
                settle();
                if (!resetn) $fatal(1, "post-relock hold release failed");
                scenarios++;
            end
        end
        $display("PASS display reset: %0d phase/mode scenarios, %0d edge checks; stopped-clock assertion and held relock", scenarios,checks);
        $finish;
    end
    initial begin
        #100000;
        $fatal(1,"display reset test timeout");
    end
endmodule

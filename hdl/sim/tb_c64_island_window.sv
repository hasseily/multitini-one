// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_island_window;
    reg clk_native=0,reset=1,pause_request=0;
    always #1 clk_native=~clk_native;
    wire [1:0] active;
    wire [31:0] status[0:1];
    genvar g;
    generate for(g=0;g<2;g=g+1)begin: standards
        multitini_c64_turbo_regs #(.CORE_CLOCK_HZ(g?32727272:31527954)) dut(
            .clk_native(clk_native),.reset(reset),.config_valid(1'b1),.config_word(32'h00000451),
            .legacy_turbo_request(1'b0),.native_tick(1'b1),.cart_present(1'b0),.loader_active(1'b0),
            .pause_request(pause_request),.guard_active(1'b1),.guard_cause(2'b00),
            .native_wr(1'b0),.native_addr(10'b0),.native_data(8'b0),
            .fio_wr(1'b0),.fio_addr(10'b0),.fio_data(8'b0),.native_rd_addr(10'b0),.fast_rd_addr(10'b0),
            .active_turbo(1'b0),.active_index(4'b0),.active_c64u_top(1'b0),
            .diag_deadline_miss(1'b0),.diag_ioonly_done(1'b0),.diag_nmi_used(1'b0),
            .policy_turbo(active[g]),.status(status[g]));
    end endgenerate
    integer n,checks=0;
    task automatic step;@(posedge clk_native);#0.01;endtask
    initial begin
        repeat(2)step();@(negedge clk_native);reset=0;
        for(n=1;n<=2045454;n++)begin
            if(n==1970497)begin
                @(negedge clk_native);pause_request=1;repeat(101)step();
                checks++;if(active!=0)$fatal(1,"WINDOW FAIL pause counted as a native tick");
                @(negedge clk_native);pause_request=0;
            end
            step();checks++;
            if(active[0]!=(n>=1970497) || status[0][19]!=(n<1970497))$fatal(1,"WINDOW FAIL PAL at %d",n);
            if(active[1]!=(n>=2045454) || status[1][19]!=(n<2045454))$fatal(1,"WINDOW FAIL NTSC at %d",n);
        end
        $display("WINDOW PASS checks=%0d pal=1970497 ntsc=2045454 pause=101",checks);$finish;
    end
endmodule

// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_island_proxy;
    reg clk_native=0,reset=1;
    always #10 clk_native=~clk_native;
    reg [4:0] ext_phase=0;
    reg ext_frame_run=1,ext_cpu_enable=0,ext_cpu_bus_valid=0;
    reg [7:0] ext_cpu_di=0;
    reg ext_irq_n=1,ext_nmi_n=1,ext_rdy=1,ext_port_tick=0,ext_dma_hold=0;
    reg policy_turbo=0,policy_blocked=0,policy_badline=1,policy_c64u_top=0;
    reg [3:0] policy_index=0;
    reg pause_request=0,cart_present=0;
    reg [15:0] isl_x_req_addr=16'hfffc;
    reg [7:0] isl_x_req_wdata=0,isl_x_req_port=8'h37;
    reg isl_x_req_we=0,isl_x_req_exact=1,isl_x_req_ioonly=0,isl_x_req_seq=0,isl_x_req_valid=1;
    wire [15:0] ext_cpu_addr;
    wire [7:0] ext_cpu_do,ext_cpu_io,b0_data;
    wire ext_cpu_we,ext_cpu_ioonly,b0_irq_n,b0_nmi_n,b0_rdy,b0_toggle,b0_exact,b0_seq;
    wire b1_toggle,b1_dma_hold,b1_turbo,b1_badline,b1_pause,b1_c64u_top;
    wire [3:0] b1_index;
    wire port_tick_toggle,irq_safe_toggle,frame_valid,frame_exact,done_seq,diag_deadline_miss;
    multitini_c64_cpu_proxy dut(.*);
    integer checks=0;
    reg previous_toggle;
    task automatic check(input bit ok,input string why);
        checks++;if(!ok)$fatal(1,"PROXY FAIL %s phase=%d addr=%x",why,ext_phase,ext_cpu_addr);
    endtask
    task automatic phase(input integer p);
        @(negedge clk_native);ext_phase=p;@(posedge clk_native);#0.01;
    endtask
    initial begin
        phase(30);check(frame_valid && ext_cpu_addr==16'hfffc,"reset-state frame");
        @(negedge clk_native);reset=0;ext_cpu_enable=1;ext_cpu_di=8'h12;
        phase(31);check(!frame_valid && ext_cpu_addr==0 && !ext_cpu_we,"exact retire becomes dummy");
        check(b0_toggle && b0_data==8'h12 && b0_exact,"exact B0");
        @(negedge clk_native);ext_cpu_enable=0;isl_x_req_seq=1;isl_x_req_addr=16'hdc0d;
        isl_x_req_we=1;isl_x_req_wdata=8'h83;
        phase(0);check(!frame_valid,"exact not loaded at phase0");
        phase(1);check(frame_valid && ext_cpu_addr==16'hdc0d && ext_cpu_we,"exact load at phase1");
        phase(31);check(frame_valid && ext_cpu_addr==16'hdc0d,"withheld exact enable retains request");
        @(negedge clk_native);ext_frame_run=0;ext_cpu_enable=1;
        phase(31);check(frame_valid && b0_toggle,"frozen phase cannot retire twice");
        @(negedge clk_native);ext_frame_run=1;ext_rdy=0;
        phase(31);check(!frame_valid && !b0_toggle && !b0_rdy,"RDY-held exact attempt retires");
        @(negedge clk_native);ext_cpu_enable=0;ext_rdy=1;isl_x_req_exact=0;
        isl_x_req_seq=0;isl_x_req_addr=16'hdf01;isl_x_req_ioonly=1;
        phase(1);check(!frame_valid,"turbo request cannot load in exact window");
        phase(14);check(!frame_valid && !ext_cpu_we,"turbo ownership sees dummy");
        phase(15);check(frame_valid && ext_cpu_addr==16'hdf01 && ext_cpu_ioonly,"turbo loads at15");
        previous_toggle=b0_toggle;
        phase(31);check(!frame_valid && ext_cpu_addr==0 && b0_toggle==previous_toggle,"BA-denied forward clears frame without completion");
        phase(15);check(frame_valid,"pending denied forward is retried");
        @(negedge clk_native);ext_cpu_bus_valid=1;ext_cpu_di=8'hb6;ext_irq_n=0;
        phase(31);check(!frame_valid && b0_toggle!=previous_toggle && b0_data==8'hb6 && !b0_irq_n,"accepted forward exactly once");
        @(negedge clk_native);ext_cpu_bus_valid=0;ext_dma_hold=1;policy_turbo=1;policy_index=15;
        phase(1);check(b1_toggle==b0_toggle && b1_dma_hold && b1_turbo && b1_index==15,"B1 includes post-trigger hold and policy");
        previous_toggle=b0_toggle;phase(15);check(!frame_valid,"completed seq never reloaded");
        @(negedge clk_native);ext_cpu_bus_valid=1;
        phase(31);check(b0_toggle==previous_toggle,"dummy bus valid is not a completion");
        @(negedge clk_native);ext_cpu_bus_valid=0;isl_x_req_seq=1;cart_present=1;
        phase(15);check(frame_valid && !ext_cpu_ioonly,"cartridge clamps I/O-only ownership");
        // Deadline diagnostics arm only after an exact retirement, and a
        // request accepted on the final legal phase14 edge meets the deadline.
        @(negedge clk_native);reset=1;isl_x_req_exact=1;isl_x_req_valid=1;isl_x_req_seq=0;policy_turbo=0;
        phase(30);@(negedge clk_native);reset=0;ext_cpu_enable=1;
        phase(31);@(negedge clk_native);ext_cpu_enable=0;isl_x_req_valid=0;
        phase(14);check(diag_deadline_miss,"missing exact request reports deadline");
        phase(15);check(!diag_deadline_miss,"deadline event is one native edge");
        @(negedge clk_native);isl_x_req_valid=1;isl_x_req_seq=1;
        phase(1);@(negedge clk_native);ext_cpu_enable=1;phase(31);
        @(negedge clk_native);ext_cpu_enable=0;isl_x_req_seq=0;
        phase(14);check(frame_valid && !diag_deadline_miss,"phase14 acceptance is on time");
        $display("PROXY PASS checks=%0d",checks);$finish;
    end
endmodule

// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Original behavioral detection sequences credit Freshness detectaudio and
// xahmol uii_audio_detect; no third-party code or bytes are copied. The bench
// supplies qualified IO accesses; CPU/adapter pacing is a separate gate.
module c64_ua_period_case #(parameter integer CORE_CLOCK_HZ=31527954);
    reg clk=0;always #5 clk=~clk;
    reg hard_reset=1,guest_reset=0,enable=1,paused=0,hold=0,reg_write=0;
    reg [7:0] reg_addr=0,reg_wdata=0;wire [7:0] reg_rdata;
    wire irq,held,memory_valid,memory_busy;wire [31:0] status,fetch_count;
    wire signed [17:0] audio_l,audio_r;wire [23:0] memory_addr;
    multitini_c64_ua #(.CORE_CLOCK_HZ(CORE_CLOCK_HZ)) dut(
        .clk(clk),.hard_reset(hard_reset),.guest_reset(guest_reset),.enable(enable),.paused(paused),.hold(hold),
        .reg_write(reg_write),.reg_addr(reg_addr),.reg_wdata(reg_wdata),.reg_rdata(reg_rdata),
        .diagnostics_armed(1'b0),.audio_tick(1'b0),.board_clip(1'b0),
        .cache_pending_addr(84'b0),.cache_pending_valid(4'b0),.cache_inflight_write_addr(21'b0),.cache_inflight_write_valid(1'b0),
        .cache_write_done(1'b0),.cache_write_done_addr(21'b0),.cache_write_snoop_valid(1'b0),.cache_write_snoop_addr(21'b0),
        .reu_memory_busy(1'b0),.irq(irq),.held(held),.status(status),.fetch_count(fetch_count),.audio_l(audio_l),.audio_r(audio_r),
        .memory_addr(memory_addr),.memory_valid(memory_valid),.memory_busy(memory_busy),.memory_ready(1'b0),
        .memory_rdata(64'b0),.memory_rvalid(1'b0),.memory_fault(1'b0));
    longint ticks=0,cycles=0,phase=0,total_ticks=0;
    integer rate_value=0,previous_tick[0:3],periods[0:3],rates[0:5];
    reg measure=0;integer checks=0,detected=0;
    always @(posedge clk)begin
        if(hard_reset || guest_reset || hold || !enable)begin phase=0;ticks=0;cycles=0;end
        else if(!paused)begin
            if(dut.half_tick !== (phase+12500000>=CORE_CLOCK_HZ))$fatal(1,"Fractional timebase mismatch");
            phase=phase+12500000;
            if(phase>=CORE_CLOCK_HZ)begin phase=phase-CORE_CLOCK_HZ;ticks=ticks+1;total_ticks=total_ticks+1;end
            cycles=cycles+1;
        end
    end
    genvar g;
    generate for(g=0;g<4;g=g+1)begin: period_check
        always @(posedge clk)if(measure && dut.voices[g].voice.boundary)begin
            if(previous_tick[g]>=0 && ticks-previous_tick[g]!=2*rate_value+3)$fatal(1,"Mode period differs voice%0d",g);
            previous_tick[g]=ticks;periods[g]=periods[g]+1;
        end
    end endgenerate
    task step;begin @(posedge clk);#2;@(negedge clk);#2;end endtask
    task write_reg(input [7:0] address,input [7:0] value);
        begin reg_addr=address;reg_wdata=value;reg_write=1;step();reg_write=0;step();end
    endtask
    task reset_guest;begin measure=0;guest_reset=1;repeat(2)step();guest_reset=0;step();end endtask
    task probe(input integer spacing,input bit stale,input bit expected);
        reg found;integer n;
        begin
            reset_guest();
            if(stale)write_reg(8'h2b,8'hff);
            // Stable version and zero status precede the IRQ probe.
            for(n=0;n<256;n=n+1)begin reg_addr=8'h20;#1;if(reg_rdata!=0)$fatal(1,"Initial detection status");step();end
            reg_addr=8'h21;#1;if(reg_rdata!=8'h10)$fatal(1,"Detection version");
            write_reg(8'h20,0);write_reg(8'h2e,0);write_reg(8'h2f,1);
            write_reg(8'h29,0);write_reg(8'h2a,1); // Low length byte deliberately retained.
            write_reg(8'h3f,8'hff);write_reg(8'h20,5);
            found=0;reg_addr=8'h20;
            for(n=0;n<128;n=n+1)begin repeat(spacing)step();if(reg_rdata[0])found=1;end
            if(found!=expected)$fatal(1,"Detection spacing%0d stale%0d expected%0d got%0d",spacing,stale,expected,found);
            detected=detected+1;
        end
    endtask
    integer i,v;longint start_ticks,start_cycles;real ppm;
    initial begin
        repeat(2)step();hard_reset=0;
        probe(352,0,1);probe(32,0,1);probe(1,0,0);probe(352,1,1);probe(32,1,0);
        rates[0]=1;rates[1]=130;rates[2]=281;rates[3]=283;rates[4]=781;rates[5]=65535;
        for(i=0;i<6;i=i+1)begin
            reset_guest();rate_value=rates[i];
            for(v=0;v<4;v=v+1)begin
                write_reg((v+1)*32+14,rate_value>>8);write_reg((v+1)*32+15,rate_value);
                previous_tick[v]=-1;periods[v]=0;
            end
            // Simultaneous setup isolates mode independence from CPU pacing.
            dut.control[0]=1;dut.control[1]=8'h11;dut.control[2]=8'h41;dut.control[3]=8'h51;
            measure=1;while(periods[0]<4)step();measure=0;
            for(v=0;v<4;v=v+1)if(periods[v]!=4)$fatal(1,"Unequal mode boundary count");
            checks=checks+4;
        end
        reset_guest();start_ticks=ticks;start_cycles=cycles;
        while(ticks-start_ticks<10000000)step();
        ppm=1000000.0*((ticks-start_ticks)*1.0*CORE_CLOCK_HZ/((cycles-start_cycles)*1.0*12500000)-1.0);
        if(ppm>10.0 || ppm< -10.0)$fatal(1,"Period ppm %f",ppm);
        // Pause freezes the same fractional phase; release does not restart it.
        paused=1;start_ticks=ticks;start_cycles=cycles;repeat(1000)step();
        if(ticks!=start_ticks || cycles!=start_cycles || dut.half_accumulator!=phase)$fatal(1,"Pause timebase");
        paused=0;repeat(100)step();
        $display("PASS: UA period clock=%0d half_ticks=10000000 modes=%0d detection=%0d ppm=%f",CORE_CLOCK_HZ,checks,detected,ppm);$finish;
    end
    initial begin #500000000;$fatal(1,"UA period timeout");end
endmodule
module tb_c64_ua_period; c64_ua_period_case test();endmodule
module tb_c64_ua_period_ntsc; c64_ua_period_case #(.CORE_CLOCK_HZ(32727266)) test();endmodule
module tb_c64_ua_period_pal_release; c64_ua_period_case #(.CORE_CLOCK_HZ(31524725)) test();endmodule
module tb_c64_ua_period_ntsc_release; c64_ua_period_case #(.CORE_CLOCK_HZ(32727273)) test();endmodule

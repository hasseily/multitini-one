// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_c64_sid_socket;
    logic clk32=0, reset=1, enableSid=0, memory_wait_q=0, pulseWr_io=0;
    logic cs_sid=0, ioe_i=0, iof_i=0;
    logic [2:0] sid_mode=0;
    logic [15:0] cpuAddr=0;
    logic [7:0] cpuDo=0;
    logic [1:0] sid_ver=0;
    wire [7:0] sid_do;
    wire sid_sel_l, sid_sel_r;
    wire [17:0] audio_l, audio_r;
    integer phase=0, native_ticks=0, checked=0, audio_cases=0, filtered_cases=0;
    bit native_run=1;
    always #5 clk32=~clk32;
    always @(negedge clk32) begin
        phase=(phase+1)%32;
        enableSid=native_run && (phase==0);
    end
    always @(posedge clk32) if(enableSid && !memory_wait_q) native_ticks++;
    // VHDL identifiers are case-insensitive and appear lowercase at the
    // mixed-language boundary; spell every mapping explicitly.
    c64_sid_socket_observed dut(.clk32(clk32),.reset(reset),.enablesid(enableSid),
        .memory_wait_q(memory_wait_q),.pulsewr_io(pulseWr_io),.cs_sid(cs_sid),
        .ioe_i(ioe_i),.iof_i(iof_i),.sid_mode(sid_mode),.cpuaddr(cpuAddr),.cpudo(cpuDo),
        .sid_ver(sid_ver),.sid_do(sid_do),.sid_sel_l(sid_sel_l),.sid_sel_r(sid_sel_r),
        .audio_l(audio_l),.audio_r(audio_r));
    task automatic check(input bit okay,input string message);
        if(okay !== 1'b1) $fatal(1,"SID %s mode=%0d addr=%04x",message,sid_mode,cpuAddr);
    endtask
    task automatic access(input bit write,input bit selected,input logic[15:0] address,input logic[7:0] value);
        @(negedge clk32); cs_sid=selected; cpuAddr=address;cpuDo=value;pulseWr_io=write;
        @(posedge clk32); #1;
    endtask
    task automatic wr(input logic[15:0] address,input logic[7:0] value);
        access(1,1,address,value);
        @(negedge clk32);pulseWr_io=0;cs_sid=0;
    endtask
    task automatic rd(input logic[15:0] address,output logic[7:0] value);
        access(0,1,address,0);value=sid_do;
        @(negedge clk32);cs_sid=0;
    endtask
    task automatic reset_sid;
        reset=1;cs_sid=0;pulseWr_io=0;native_run=1;
        repeat(96) @(negedge clk32);
        reset=0;
        repeat(64) @(negedge clk32);
    endtask
    task automatic oscillator(input logic[15:0] base,input logic[15:0] frequency);
        wr(base+14,frequency[7:0]);wr(base+15,frequency[15:8]);
        wr(base+19,8'h00);wr(base+20,8'hf0);wr(base+18,8'h21);
        wr(base+23,8'h00);wr(base+24,8'h0f);
    endtask
    initial begin : run
        integer fd,status,mode_i,enabled,address,selected,rows,model_i,positive;
        logic[7:0] left_bus,right_bus,value,readback,frozen_osc;
        logic[15:0] right_base;
        logic[17:0] last_l,last_r;
        integer changing_l,changing_r,different,filter_index,last_filter_difference;
        integer settling_differences,settling_max_delta,settling_delta;
        string vectors;
        if(!$value$plusargs("VECTORS=%s",vectors)) vectors="sid_vectors.txt";
        for(model_i=0;model_i<2;model_i++) begin
            sid_ver=model_i?2'b11:2'b00;
            reset_sid();
            fd=$fopen(vectors,"r");check(fd!=0,"cannot open reference vectors");rows=0;
            while(!$feof(fd)) begin
                status=$fscanf(fd,"%h %h %h %h\n",mode_i,enabled,address,selected);
                check(status==4,"bad reference vector");
                if((rows%2048)==0) begin
                    sid_mode=mode_i;right_base=(mode_i==3)?16'hd500:16'hd420;
                    wr(16'hd400,8'h31);wr(right_base,8'h72);
                    left_bus=(mode_i==0)?8'h72:8'h31;right_bus=8'h72;
                end
                value=8'((rows*37+model_i*19+1)^8'hc5);
                access(1,enabled,address,value);
                check(sid_sel_l === (enabled && (mode_i==0 || selected==0)),"left select differs from MAME");
                check(sid_sel_r === (enabled && (mode_i==0 || selected==1)),"right select differs from MAME");
                if(enabled) begin
                    if(mode_i==0 || selected==0) left_bus=value;
                    if(mode_i==0 || selected==1) right_bus=value;
                    check(sid_do===value,"selected write/read latch");
                end
                rd(16'hd400,readback);check(readback===left_bus,$sformatf("left write isolation row=%0d got=%02x expected=%02x",rows,readback,left_bus));
                rd(right_base,readback);check(readback===right_bus,$sformatf("right write isolation row=%0d got=%02x expected=%02x",rows,readback,right_bus));
                rows++;checked++;
            end
            $fclose(fd);check(rows==6144,"incomplete reference vector set");
            for(mode_i=0;mode_i<3;mode_i++) begin
                sid_mode=mode_i==0?0:(mode_i==1?2:3);
                right_base=sid_mode==3?16'hd500:16'hd420;
                reset_sid();
                rd(16'hd419,readback);check(readback===8'h12,"left POTX callback");
                rd(16'hd41a,readback);check(readback===8'h34,"left POTY callback");
                rd(right_base+25,readback);check(readback===(sid_mode==0?8'h12:8'hff),"second POTX is unconnected");
                rd(right_base+26,readback);check(readback===(sid_mode==0?8'h34:8'hff),"second POTY is unconnected");
                oscillator(16'hd400,16'h3210);
                if(sid_mode!=0) oscillator(right_base,16'h0789);
                changing_l=0;changing_r=0;different=0;last_l=0;last_r=0;
                repeat(4096) begin
                    repeat(32) @(negedge clk32);
                    check(!$isunknown({audio_l,audio_r}),"unknown audio sample");
                    if(audio_l!=last_l) changing_l++;
                    if(audio_r!=last_r) changing_r++;
                    if(audio_l!=audio_r) different++;
                    last_l=audio_l;last_r=audio_r;
                end
                check(changing_l>32 && changing_r>32,$sformatf("SID oscillators did not produce audio l=%0d r=%0d samples=%h/%h",changing_l,changing_r,audio_l,audio_r));
                if(sid_mode==0) check(different==0,"single SID outputs differ");
                else check(different>32,"dual SID stereo channels did not separate");
                $display("SID_AUDIO model=%0d mode=%0d changing_l=%0d changing_r=%0d distinct=%0d",model_i,sid_mode,changing_l,changing_r,different);
                // Route the active voice through the actual low-pass filter.
                // Unknown integrator startup must fail, and both chips must
                // still emit changing samples through the filtered route.
                wr(16'hd415,8'h00);wr(16'hd416,8'h80);
                wr(16'hd417,8'h04);wr(16'hd418,8'h1f);
                if(sid_mode!=0) begin
                    wr(right_base+21,8'h00);wr(right_base+22,8'h80);
                    wr(right_base+23,8'h04);wr(right_base+24,8'h1f);
                end
                // Analog integrators intentionally retain prior chip history
                // across register reset. The decode vectors programmed the
                // banks differently, and filter writes may land between the
                // shared MAC's left and right phases. Measure a bounded 1024
                // native-tick settling period, then demand exact mono pairs
                // throughout the following 4096 samples (no tolerance).
                settling_differences=0;settling_max_delta=0;last_filter_difference=-1;
                for(filter_index=0;filter_index<1024;filter_index++) begin
                    repeat(32) @(negedge clk32);
                    check(!$isunknown({audio_l,audio_r}),"unknown filtered settling sample");
                    if(audio_l!=audio_r) begin
                        settling_differences++;last_filter_difference=filter_index;
                    end
                    settling_delta=$signed(audio_l)-$signed(audio_r);
                    if(settling_delta<0) settling_delta=-settling_delta;
                    if(settling_delta>settling_max_delta) settling_max_delta=settling_delta;
                end
                $display("SID_FILTER_SETTLE model=%0d mode=%0d samples=1024 differing=%0d last_difference=%0d maximum_delta=%0d",model_i,sid_mode,settling_differences,last_filter_difference,settling_max_delta);
                changing_l=0;changing_r=0;different=0;
                last_filter_difference=-1;
                for(filter_index=0;filter_index<4096;filter_index++) begin
                    repeat(32) @(negedge clk32);
                    check(!$isunknown({audio_l,audio_r}),"unknown filtered audio sample");
                    if(audio_l!=last_l) changing_l++;
                    if(audio_r!=last_r) changing_r++;
                    if(audio_l!=audio_r) begin different++;last_filter_difference=filter_index;end
                    last_l=audio_l;last_r=audio_r;
                end
                check(changing_l>32 && changing_r>32,"filtered audio is not changing");
                if(sid_mode==0) check(different==0,$sformatf("single filtered outputs differ after settling count=%0d last=%0d final=%h/%h",different,last_filter_difference,audio_l,audio_r));
                else check(different>32,"filtered stereo channels did not separate");
                $display("SID_FILTER model=%0d mode=%0d changing_l=%0d changing_r=%0d distinct=%0d",model_i,sid_mode,changing_l,changing_r,different);
                filtered_cases++;
                native_run=0;repeat(64) @(negedge clk32);
                rd(16'hd41b,frozen_osc);
                repeat(100) begin
                    wr(16'hd415,8'h06);rd(16'hd41b,readback);
                    check(readback===frozen_osc,"CPU writes advanced stopped native oscillator");
                end
                native_run=1;
                audio_cases++;
            end
        end
        check(checked==12288 && audio_cases==6 && filtered_cases==6,"incomplete coverage");
        $display("PASS: C64 SID socket MAME vectors=%0d audio_cases=%0d filtered_cases=%0d native_ticks=%0d",checked,audio_cases,filtered_cases,native_ticks);
        $finish;
    end
    initial begin #50000000; $fatal(1,"SID test timeout");end
endmodule

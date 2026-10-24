// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_c64_sid_map_mix;
    reg clk=0;always #5 clk=~clk;
    reg reset=1,word_valid=0,commit=0,ce_1m=0,paused=0;
    reg [5:0] word_index=0;reg [31:0] word_data=0;reg [7:0] commit_seq=0;
    wire active_valid,rejected;wire [7:0] active_seq;
    wire [255:0] slot_masks,chip_params;wire [31:0] write_inhibit,output_config;
    wire [95:0] owners;wire [127:0] io_slot_masks;
    multitini_c64_sid_map dut(.*);
    reg sample_tick=0,map_enable=1;reg [143:0] audio=0;reg [255:0] params=0;reg [31:0] options=32'h00008080;
    reg signed [17:0] ua_audio_l=0,ua_audio_r=0;wire signed [23:0] audio_l,audio_r;wire clip;
    multitini_c64_sid_mix mix(.clk(clk),.reset(reset),.ce_1m(ce_1m),.sample_tick(sample_tick),.map_enable(map_enable),
        .chip_audio(audio),.chip_params(params),.output_config(options),.ua_audio_l(ua_audio_l),.ua_audio_r(ua_audio_r),.audio_l(audio_l),.audio_r(audio_r),.clip(clip));
    integer checks=0,i,t;reg [31:0] words[0:56];integer expected_l,expected_r;
    task automatic check(input bit ok,input string why);checks++;if(!ok)$fatal(1,"MAP/MIX %s l=%d r=%d clip=%d",why,audio_l,audio_r,clip);endtask
    task automatic step;@(posedge clk);#1;endtask
    task automatic upload;
        for(integer j=0;j<57;j++)begin @(negedge clk);word_valid=1;word_index=j;word_data=words[j];step();end
        @(negedge clk);word_valid=0;
    endtask
    task automatic send_commit;
        @(negedge clk);commit=1;step();@(negedge clk);commit=0;
    endtask
    initial begin
        $readmemh("table.hex",words);repeat(3)step();@(negedge clk);reset=0;
        send_commit();check(!active_valid,"partial commit rejected");
        upload();commit_seq=8'hff;send_commit();repeat(3)step();check(!active_valid,"waits safe boundary");
        @(negedge clk);paused=1;step();check(active_valid&&active_seq==255,"paused atomic swap");
        for(i=0;i<32;i++)begin check(slot_masks[i*8+:8]==words[i][7:0],"all active slots");check(slot_masks[i*8+owners[i*3+:3]],"owner in mask");end
        paused=0;words[7]=0;upload();commit_seq=0;send_commit();ce_1m=1;step();ce_1m=0;check(active_seq==255,"invalid map preserves active");
        words[7]=8;upload();send_commit();ce_1m=1;step();ce_1m=0;check(active_seq==0,"sequence wrap atomic");
        // Point-mode unity, signed widening, saturation and headroom.
        for(t=-1000;t<=1000;t+=125)begin
            params=0;params[7:0]=128;params[47:40]=128;audio[17:0]=t;audio[35:18]=-t;ua_audio_l=17;ua_audio_r=-31;#1;
            check(audio_l==((t+17)*64)&&audio_r==((-t-31)*64)&&!clip,"legacy mix bit equality");
        end
        for(i=0;i<8;i++)begin audio[i*18+:18]=18'sh1ffff;params[i*32+:16]=16'hffff;end
        ua_audio_l=131071;ua_audio_r=131071;options=32'h0000ffff;#1;check(clip&&audio_l==8388607&&audio_r==8388607,"positive saturation no wrap");
        for(i=0;i<8;i++)audio[i*18+:18]=-131072;ua_audio_l=-131072;ua_audio_r=-131072;#1;check(clip&&audio_l==-8388608,"negative saturation no wrap");
        params=0;audio=0;ua_audio_l=0;ua_audio_r=0;params[7:0]=128;audio[17:0]=17000;options=32'h00028080;
        #1;check(audio_l==17000*64,"DC enable has no immediate step");
        for(t=0;t<163840;t++)begin @(negedge clk);ce_1m=1;step();@(negedge clk);ce_1m=0;repeat(9)step();end
        check(audio_l>0&&audio_l<150*64,"positive DC decays in five time constants");
        // A fresh constant-negative average retains its sign and exact quotient.
        @(negedge clk);reset=1;step();@(negedge clk);reset=0;options=32'h00018080;audio[17:0]=-1234;
        repeat(100)step();@(negedge clk);sample_tick=1;step();@(negedge clk);sample_tick=0;repeat(45)step();
        check(audio_l==-1234*64&&audio_r==0,"average signed division");
        $display("PASS SID MAP MIX checks=%0d",checks);$finish;
    end
    initial begin #20000000;$fatal(1,"map/mix timeout");end
endmodule

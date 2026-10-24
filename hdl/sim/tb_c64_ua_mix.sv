// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_c64_ua_mix;
    reg clk=0;always #5 clk=~clk;
    reg reset=1,audio_tick=0;
    reg [111:0] samples=0;
    reg [41:0] volumes=0;
    reg [27:0] pans=0;
    wire signed [17:0] audio_l,audio_r;
    wire clipped;
    multitini_c64_ua_mix dut(.*);
    integer file,rc,n,expected_l,expected_r,cases=0,k,interval;
    reg [111:0] next_samples;reg [41:0] next_volumes;reg [27:0] next_pans;
    task step;begin @(posedge clk);#1;@(negedge clk);end endtask
    initial begin
        file=$fopen("mix_vectors.txt","r");
        while(!$feof(file)) begin
            rc=$fscanf(file,"%h %h %h %d %d\n",next_samples,next_volumes,next_pans,expected_l,expected_r);
            if(rc!=5) $fatal(1,"Malformed mix vector");
            reset=1;step();reset=0;samples=next_samples;volumes=next_volumes;pans=next_pans;
            repeat(8) step();
            if($signed(dut.mix_l)>>>3 != expected_l || $signed(dut.mix_r)>>>3 != expected_r)
                $fatal(1,"Mix vector %0d expected %0d/%0d got %0d/%0d",cases,expected_l,expected_r,$signed(dut.mix_l)>>>3,$signed(dut.mix_r)>>>3);
            cases=cases+1;
        end
        $fclose(file);
        for(k=0;k<12;k=k+1) begin
            samples=0;volumes=0;pans=0;
            samples[15:0]=(k%3==0)?16'h8000:((k%3==1)?16'h7fff:16'hffff);
            volumes[5:0]=63;pans[3:0]=8;
            expected_l=($signed(samples[15:0])*63)>>>5;expected_l=(expected_l*7)>>>3;expected_r=expected_l;
            reset=1;step();reset=0;
            repeat(16)step();audio_tick=1;step();audio_tick=0;
            interval=k<4 ? (k+1)*8 : ((k%4==0)?82:((k%4==1)?83:((k%4==2)?85:86)))*8;
            repeat(interval-1) step();audio_tick=1;step();audio_tick=0;
            repeat(32) step();
            if(dut.normalized_l!=expected_l || dut.normalized_r!=expected_r)
                $fatal(1,"Normalize interval %0d expected %0d got %0d",interval,expected_l,dut.normalized_l);
            audio_tick=1;step();audio_tick=0;
            if(audio_l!=expected_l || audio_r!=expected_r) $fatal(1,"Normalize output pipeline");
        end
        $display("PASS: UA mix vectors=%0d normalization=12",cases);$finish;
    end
endmodule

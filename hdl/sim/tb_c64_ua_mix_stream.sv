// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_c64_ua_mix_stream;
    reg clk=0;always #5 clk=~clk;
    reg reset=1,audio_tick=0;reg [111:0] samples=0;
    reg [41:0] volumes=0;reg [27:0] pans=0;
    wire signed [17:0] audio_l,audio_r;wire clipped;
    multitini_c64_ua_mix dut(.*);
    integer input_file,output_file,rc,k,cycle=0,frames=0,audio_phase=0,left_value,right_value;
    task step;begin @(posedge clk);#1;@(negedge clk);end endtask
    initial begin
        input_file=$fopen("mix_stream_vectors.txt","r");output_file=$fopen("mix_stream.csv","w");
        $fdisplay(output_file,"clock,left,right");step();reset=0;
        while(!$feof(input_file))begin
            rc=$fscanf(input_file,"%h %h %h %d %d\n",samples,volumes,pans,left_value,right_value);
            if(rc!=5)$fatal(1,"Malformed continuous mix input");
            for(k=0;k<8;k=k+1)begin
                audio_phase+=48000;audio_tick=audio_phase>=31527954;
                if(audio_tick)audio_phase-=31527954;
                step();cycle+=1;
                if(k==7 && ($signed(dut.mix_l)>>>3!=left_value || $signed(dut.mix_r)>>>3!=right_value))
                    $fatal(1,"Continuous mix mismatch frame%0d",frames);
                if(audio_tick)$fdisplay(output_file,"%0d,%0d,%0d",cycle,audio_l,audio_r);
            end
            frames+=1;
        end
        if(cycle!=2000000)$fatal(1,"Continuous mixer coverage");
        $fclose(input_file);$fclose(output_file);
        $display("PASS: UA continuous mix clocks=%0d frames=%0d",cycle,frames);$finish;
    end
endmodule

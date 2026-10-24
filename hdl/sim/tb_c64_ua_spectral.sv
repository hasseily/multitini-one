// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Native-time tone transitions follow the independent half-tick period law.
module tb_c64_ua_spectral #(parameter integer CORE_CLOCK_HZ=31524725);
    reg clk=0;always #5 clk=~clk;
    reg reset=1,audio_tick=0;reg [15:0] source_sample=0;
    wire signed [17:0] audio_l,audio_r;wire clipped;
    multitini_c64_ua_mix dut(.clk(clk),.reset(reset),.audio_tick(audio_tick),.samples({96'b0,source_sample}),
        .volumes(42'd32),.pans(28'd8),.audio_l(audio_l),.audio_r(audio_r),.clipped(clipped));
    integer input_file,output_file,rc,cycle=0,audio_phase=0,outputs=0;
    integer rate_value=0,half_phase=0,remaining=0,position=0;
    reg [15:0] tone[0:16383];
    always @(posedge clk)begin
        if(reset)begin half_phase=0;remaining=2*rate_value+3;position=0;source_sample<=0;end
        else begin
            half_phase+=12500000;
            if(half_phase>=CORE_CLOCK_HZ)begin
                half_phase-=CORE_CLOCK_HZ;
                if(remaining>1)remaining-=1;
                else begin source_sample<=tone[position];position+=1;remaining=2*rate_value+3;end
            end
        end
    end
    task step;begin @(posedge clk);#1;@(negedge clk);end endtask
    initial begin
        input_file=$fopen("tone_config.txt","r");rc=$fscanf(input_file,"%d",rate_value);$fclose(input_file);
        if(rc!=1)$fatal(1,"Malformed tone config");$readmemh("tone_samples.txt",tone);
        output_file=$fopen("spectral.csv","w");
        $fdisplay(output_file,"clock,left,right");step();reset=0;
        while(outputs<9217)begin
            audio_phase+=48000;audio_tick=audio_phase>=CORE_CLOCK_HZ;if(audio_tick)audio_phase-=CORE_CLOCK_HZ;
            step();cycle+=1;
            if(audio_tick)begin $fdisplay(output_file,"%0d,%0d,%0d",cycle,audio_l,audio_r);outputs+=1;end
        end
        $fclose(output_file);
        $display("PASS: UA spectral mix clocks=%0d samples=%0d",cycle,outputs);$finish;
    end
endmodule

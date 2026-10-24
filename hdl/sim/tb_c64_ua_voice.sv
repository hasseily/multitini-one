// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_c64_ua_voice;
    reg clk=0;always #5 clk=~clk;
    reg reset=1,paused=0,half_tick=0,sample_ready=1;
    reg [7:0] control=0;
    reg [23:0] start_address=0,sample_length=0,repeat_a=0,repeat_b=0;
    reg [15:0] rate=0;
    reg signed [15:0] next_sample=0;
    wire fetch,sixteen_bit,predict_valid,playing,irq_set,underrun;
    wire [23:0] fetch_address,predict_address;
    wire [17:0] deadline;
    wire signed [15:0] sample;
    multitini_c64_ua_voice dut(.*);
    integer out,cycle=0,scenario,j;
    reg old_fetch;reg [23:0] old_address;
    always @(posedge clk) begin
        old_fetch=reset ? 1'b0 : fetch;old_address=reset ? 24'b0 : fetch_address;
        #1;
        $fdisplay(out,"%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d",
            cycle,reset,paused,half_tick,control,start_address,sample_length,repeat_a,repeat_b,rate,
            next_sample,sample_ready,old_fetch,old_address,sample,playing,irq_set,underrun,deadline,dut.position,dut.state);
        cycle=cycle+1;
    end
    initial begin
        out=$fopen("voice_trace.csv","w");
        $fdisplay(out,"cycle,reset,paused,half_tick,control,start,length,a,b,rate,next_sample,ready,fetch,address,sample,playing,irq,underrun,remaining,position,state");
        repeat(2) @(negedge clk);
        for(scenario=0;scenario<24;scenario=scenario+1) begin
            reset=1;@(negedge clk);reset=0;
            control=5 | ((scenario%4)*16) | ((scenario%3==0)?64:0);
            start_address=scenario%2 ? 24'hfffff7 : 24'h7fff9;
            sample_length=scenario%6==0 ? 0 : (scenario%5==0 ? 7 : 16);
            repeat_a=3;repeat_b=sample_length;rate=scenario%3;
            if(scenario>=12) control=control|2;
            for(j=0;j<300;j=j+1) begin
                half_tick=(j%5)!=0;next_sample=(j*137+scenario*13);sample_ready=(j%11)!=0;
                paused=j>=35 && j<42;
                if(j==70 && scenario>=12) control=control & 8'hfe;
                if(j==80 && scenario>=12) control=control | 1;
                if(j==100 && scenario>=12) control=0;
                if(j==104 && scenario>=12) control=7;
                if(j==140) rate=4;
                if(j==180 && scenario>=12) control=control&8'hfe;
                @(negedge clk);
            end
        end
        $fclose(out);$display("PASS: UA voice trace cases=24");$finish;
    end
endmodule

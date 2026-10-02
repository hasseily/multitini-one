// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_video_frame_filter;
    parameter integer WIDTH=13,HEIGHT=9,JOBS=448,STALL=1;
    parameter integer SCALE_NUM=3,SCALE_DEN=2;
    parameter integer OUTPUT_SCALE_NUM=SCALE_NUM,OUTPUT_SCALE_DEN=SCALE_DEN;
    parameter bit DYNAMIC_DISPLAY=1'b0;
    localparam integer PIXELS=WIDTH*HEIGHT;
    reg clk=0,resetn=0,start=0,in_valid=0,out_ready=0;
    always #3.75 clk=!clk;
    reg [7:0] options=0;
    reg [3:0] display_scale_num=4'(SCALE_NUM),display_scale_den=4'(SCALE_DEN);
    reg [23:0] in_rgb=0;
    wire in_ready,out_valid,busy,done;
    wire [15:0] out_rgb565;
    multitini_video_frame_filter #(.WIDTH(WIDTH),.HEIGHT(HEIGHT),
        .SCALE_NUM(SCALE_NUM),.SCALE_DEN(SCALE_DEN),.DYNAMIC_DISPLAY(DYNAMIC_DISPLAY)) dut(.*);
    reg [23:0] inputs[0:JOBS*PIXELS-1];
    reg [15:0] expected[0:JOBS*PIXELS-1];
    reg [7:0] configurations[0:JOBS-1];
    string directory;
    integer job=0,sent=0,received=0,cycles=0,start_cycle=0,total=0,job_cycles=0;
    reg running=0,in_accepted=0,previous_stalled=0;
    reg [15:0] previous_output=0;
    reg [31:0] random_state=32'h927418ab;
    always @(posedge clk)begin
        cycles=cycles+1;
        if(!resetn)begin in_accepted=0;previous_stalled=0;end
        else begin
            in_accepted=in_valid && in_ready;
            if(in_accepted)begin
                sent=sent+1;
                if(sent>PIXELS)$fatal(1,"Filter accepted extra input");
            end
            if(previous_stalled && (!out_valid || out_rgb565!==previous_output))
                $fatal(1,"Output changed under backpressure");
            previous_stalled=out_valid && !out_ready;
            previous_output=out_rgb565;
            if(out_valid && out_ready)begin
                if(received>=PIXELS)$fatal(1,"Filter emitted extra output");
                if(out_rgb565!==expected[job*PIXELS+received])
                    $fatal(1,"Filter mismatch job%0d pixel%0d options%h got%h expected%h",
                        job,received,configurations[job],out_rgb565,expected[job*PIXELS+received]);
                received=received+1;total=total+1;
            end
        end
    end
    always @(negedge clk)begin
        if(running && resetn)begin
            // A job owns its launch configuration even while stalled. Change
            // the external controls after launch to catch live blend decode.
            options=~configurations[job];
            display_scale_num=SCALE_NUM==4 ? 4'd6 : 4'd4;
            display_scale_den=1;
            random_state=random_state^(random_state<<13);
            random_state=random_state^(random_state>>17);
            random_state=random_state^(random_state<<5);
            out_ready=STALL==0 || random_state[1] || random_state[4];
            if(!in_valid || in_accepted)begin
                in_valid=sent<PIXELS && (STALL==0 || random_state[0] || random_state[3]);
                if(sent<PIXELS)in_rgb=inputs[job*PIXELS+sent];
            end
        end
    end
    task begin_job;
        begin
            @(negedge clk);sent=0;received=0;options=configurations[job];start=1;
            display_scale_num=4'(OUTPUT_SCALE_NUM);display_scale_den=4'(OUTPUT_SCALE_DEN);
            start_cycle=cycles;in_valid=0;out_ready=0;running=0;
            @(negedge clk);start=0;running=1;
        end
    endtask
    initial begin
        if(!$value$plusargs("vectors=%s",directory))$fatal(1,"Missing vector directory");
        $readmemh({directory,"/input.hex"},inputs);
        $readmemh({directory,"/expected.hex"},expected);
        $readmemh({directory,"/options.hex"},configurations);
        repeat(4)@(negedge clk);resetn=1;
        // Aborting mid-row must invalidate the pipeline, then a new job must
        // overwrite old line history before any pixel can become observable.
        if(STALL!=0)begin
            begin_job();wait(sent>=((PIXELS<10)?PIXELS:10));
            @(negedge clk);running=0;resetn=0;in_valid=0;out_ready=0;
            repeat(3)@(negedge clk);
            if(busy || out_valid || done)$fatal(1,"Reset did not cancel the old filter job");
            resetn=1;
            begin_job();wait(out_valid);
            @(negedge clk);running=0;in_valid=0;out_ready=0;
            repeat(3)@(negedge clk);
            if(!out_valid)$fatal(1,"Expected a held output before reset");
            resetn=0;
            repeat(3)@(negedge clk);
            if(busy || out_valid || done)$fatal(1,"Reset retained a held output");
            resetn=1;
        end
        for(job=0;job<JOBS;job=job+1)begin
            begin_job();
            wait(done);#1;
            job_cycles=cycles-start_cycle;
            if(sent!=PIXELS || received!=PIXELS || busy)$fatal(1,"Incorrect completion counts/state");
            if(STALL==0 && job_cycles>(WIDTH+2)*(HEIGHT+2)+12)
                $fatal(1,"Flowing filter exceeded fixed cycle budget: %0d",job_cycles);
            @(negedge clk);running=0;in_valid=0;out_ready=0;
            repeat(3)@(negedge clk);
            if(done || out_valid)$fatal(1,"Job completion/output repeated");
        end
        $display("PASS native frame filter: %0dx%0d, %0d jobs, %0d pixels, stalls=%0d, last cycles=%0d budget=%0d",
            WIDTH,HEIGHT,JOBS,total,STALL,job_cycles,(WIDTH+2)*(HEIGHT+2)+12);
        $finish;
    end
    initial begin #100000000;$fatal(1,"Filter simulation timeout");end
endmodule

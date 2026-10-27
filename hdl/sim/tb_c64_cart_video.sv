// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Two synthetic native video sources produce the same pixel sequence. One
// advances continuously; one stops at every phase for varying cache waits.
// cart_video_lane is the exact production adapter video block, extracted by
// test_c64_cart_video.py, connected to the production video_sync entity.
module tb_c64_cart_video #(parameter integer NTSC=0);
    localparam integer LINE_CLOCKS=(NTSC?65:63)*32;
    localparam integer LINES=NTSC?263:312;
    localparam integer FRAME_PIXELS=LINE_CLOCKS*LINES/4;
    reg clk=0,reset=1,stalled_run=1,stalled_request=1;
    always #5 clk=~clk;
    // Like FPGA64 memory_wait_q, eligibility changes after the rising edge;
    // the VIC consumes its old value at that same edge.
    always @(posedge clk)if(reset)stalled_run<=1;else stalled_run<=stalled_request;
    integer step[0:1]='{0,0};
    wire [1:0] run={stalled_run,1'b1};
    wire [1:0] valid,hs,vs,de;
    wire [7:0] r[0:1],g[0:1],b[0:1];
    reg [26:0] golden[0:FRAME_PIXELS+1023];
    integer frame[0:1]='{0,0},count[0:1]='{0,0};
    reg [1:0] old_vs=0,recording=0,done=0;
    integer stalls=0,remaining=0,last_trigger=-1;
    reg [15:0] coverage=0;
    for(genvar lane=0;lane<2;lane++)begin:lanes
        wire [7:0] source_r=(step[lane]/4)%256;
        wire [7:0] source_g=(step[lane]/LINE_CLOCKS)%256;
        wire [7:0] source_b=source_r^source_g^8'ha5;
        wire source_hs=(step[lane]%LINE_CLOCKS)>=64&&(step[lane]%LINE_CLOCKS)<128;
        wire source_vs=((step[lane]/LINE_CLOCKS)%LINES)<3;
        always @(posedge clk)if(reset)step[lane]<=0;else if(run[lane])step[lane]<=step[lane]+1;
        cart_video_lane #(.NTSC_MODE(NTSC)) capture(
            .core_clk(clk),.core_reset_n(!reset),.core_paused(1'b0),.ext_frame_run(run[lane]),
            .raw_r(source_r),.raw_g(source_g),.raw_b(source_b),
            .raw_hsync(source_hs),.raw_vsync(source_vs),
            .video_r(r[lane]),.video_g(g[lane]),.video_b(b[lane]),
            .video_de(de[lane]),.video_hsync(hs[lane]),.video_vsync(vs[lane]),.video_valid(valid[lane]));
    end
    // Every combination of native divider phase and hold-length modulo four.
    always @(negedge clk)begin
        if(reset)begin stalled_request=1;remaining=0;end
        else if(remaining>0)begin stalled_request=0;remaining--;end
        else if(step[1]%97==96&&step[1]!=last_trigger)begin
            last_trigger=step[1];remaining=stalls%17;
            coverage[((step[1]+1)%4)*4+((remaining+1)%4)]=1;
            stalls++;stalled_request=0;
        end else stalled_request=1;
    end
    always @(posedge clk)begin
        #1;
        if(!reset)for(integer lane=0;lane<2;lane++)if(valid[lane]&&!done[lane])begin
            if(vs[lane]&&!old_vs[lane])begin
                frame[lane]++;
                if(frame[lane]==2)recording[lane]=1;
                if(frame[lane]==3)begin recording[lane]=0;done[lane]=1;end
            end
            old_vs[lane]=vs[lane];
            if(recording[lane])begin
                if(count[lane]>=FRAME_PIXELS+1024)$fatal(1,"VIDEO tuple stream overrun");
                if($isunknown({r[lane],g[lane],b[lane],de[lane],hs[lane],vs[lane]}))
                    $fatal(1,"VIDEO unknown tuple lane=%0d",lane);
                if(lane==0)golden[count[0]]={r[0],g[0],b[0],de[0],hs[0],vs[0]};
                else begin
                    if(count[1]>=count[0])$fatal(1,"VIDEO comparison ahead of reference");
                    if(golden[count[1]]!=={r[1],g[1],b[1],de[1],hs[1],vs[1]})
                        $fatal(1,"VIDEO tuple mismatch NTSC=%0d index=%0d expected=%h actual=%h",NTSC,count[1],golden[count[1]],{r[1],g[1],b[1],de[1],hs[1],vs[1]});
                end
                count[lane]++;
            end
        end
        if(&done)begin
            if(count[0]!=count[1]||count[0]!=FRAME_PIXELS||coverage!=16'hffff)
                $fatal(1,"VIDEO incomplete frame/phase coverage %0d/%0d expected=%0d phases=%h",count[0],count[1],FRAME_PIXELS,coverage);
            $display("VIDEO TUPLES PASS NTSC=%0d tuples=%0d phase_length_pairs=16 stalls=%0d",NTSC,count[0],stalls);
            $finish;
        end
    end
    initial begin repeat(16)@(negedge clk);reset=0;end
    initial begin #(LINE_CLOCKS*LINES*120);$fatal(1,"VIDEO differential timeout");end
endmodule

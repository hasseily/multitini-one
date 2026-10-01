// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Checks only geometry and the reset barrier. AXI/FIFO capture behavior is
// exercised separately by tb_video_capture and the frame-pipeline tests.
module tb_capture_geometry;
    localparam integer WIDTH=6,HEIGHT=4,PAD=58;
    reg clk=0,pixel_clk=0,resetn=0,pixel_resetn=0;
    always #5 clk=~clk;
    always #4 pixel_clk=~pixel_clk;
    reg [11:0] input_x=0,input_y=0,input_w=0,input_h=0,scan_x=0,scan_y=0;
    reg [3:0] input_num=1,input_den=1;
    multitini_video_capture #(.DYNAMIC_DISPLAY(1),.WIDTH(WIDTH),.HEIGHT(HEIGHT)) dut(
        .display_view_x(input_x),.display_view_y(input_y),.display_view_width(input_w),
        .display_view_height(input_h),.display_scale_num(input_num),.display_scale_den(input_den),
        .clk(clk),.resetn(resetn),.enable(1'b0),.consumer(32'd0),
        .pixel_clk(pixel_clk),.pixel_resetn(pixel_resetn),.shared_pixel_resetn(1'b0),
        .vblank_start(1'b0),.machine_ready(1'b1),.scan_x(scan_x),.scan_y(scan_y),
        .rgb565(16'h5a3c),.awready(1'b0),.wready(1'b0),.bresp(2'd0),.bvalid(1'b0));

    integer ox,oy,vw,vh,num,den,right12,bottom12,pad_right;
    integer cases=0,coordinates=0,origin_zero_phase_edges=0;
    integer column_phase,row_phase,phase_y;
    reg expected_row,expected_native,expected_padding;
    task check_config;
        begin
            if({dut.view_x,dut.view_y,dut.view_right,dut.view_bottom,dut.padding_right,
                dut.scale_num,dut.scale_den} !==
                {12'(ox),12'(oy),12'(right12),12'(bottom12),14'(pad_right),4'(num),4'(den)})
                $fatal(1,"capture config mismatch case=%0d at %0d,%0d",cases,scan_x,scan_y);
        end
    endtask

    task configure(input integer x0,y0,width0,height0,n0,d0,release_together);
        begin
            @(negedge pixel_clk);pixel_resetn=0;scan_x=0;scan_y=0;
            repeat(3)@(negedge pixel_clk);
            if(dut.pixel_run)$fatal(1,"capture did not enter reset");
            // Prove a second configuration supersedes an earlier reset-held
            // configuration; alternate settling before and at reset release.
            input_x=17;input_y=19;input_w=23;input_h=29;input_num=4;input_den=1;
            @(negedge pixel_clk);
            ox=x0;oy=y0;vw=width0;vh=height0;num=n0;den=d0;
            right12=(ox+vw)%4096;bottom12=(oy+vh)%4096;pad_right=ox+vw+PAD;
            input_x=12'(ox);input_y=12'(oy);input_w=12'(vw);input_h=12'(vh);
            input_num=4'(num);input_den=4'(den);
            if(!release_together)begin
                @(posedge pixel_clk);#1;check_config();
                @(negedge pixel_clk);
            end
            pixel_resetn=1;
            for(integer edge_count=1;edge_count<=3;edge_count=edge_count+1)begin
                @(posedge pixel_clk);#1;
                if(dut.pixel_run !== (edge_count==3))
                    $fatal(1,"capture release barrier changed at edge %0d",edge_count);
                check_config();
            end
            // All live geometry inputs now differ. The active configuration
            // must remain frozen for the entire following raster.
            input_x=37;input_y=41;input_w=43;input_h=47;input_num=7;input_den=2;
            cases=cases+1;
        end
    endtask

    task raster(input integer line_total,frame_total);
        begin
            for(integer y=0;y<frame_total;y=y+1)begin
                for(integer x=0;x<line_total;x=x+1)begin
                    @(negedge pixel_clk);scan_x=12'(x);scan_y=12'(y);#1;
                    check_config();
                    // The preexisting phase tracker advances row phase on
                    // the edge consuming x=0. Preserve that behavior: at x=0
                    // the prior line's phase is still visible. Production
                    // scaled viewports have a nonzero horizontal origin.
                    phase_y=x==0 ? y-1 : y;
                    row_phase=(phase_y<=oy || phase_y>=bottom12) ? 0 : (phase_y-oy)%num;
                    column_phase=x>=ox ? (x-ox)%num : 0;
                    expected_row=y>=oy && y<bottom12 && (den==2 ? row_phase!=1 : row_phase==0);
                    expected_native=x>=ox && x<right12 && (den==2 ? column_phase!=1 : column_phase==0);
                    expected_padding=x>=right12 && x<pad_right;
                    if(dut.selected_row !== expected_row || dut.native_sample !== expected_native ||
                       dut.padding_sample !== expected_padding)
                        $fatal(1,"capture sample mismatch case=%0d xy=%0d,%0d ratio=%0d/%0d got=%b%b%b expected=%b%b%b phase=%0d,%0d",
                            cases,x,y,num,den,dut.selected_row,dut.native_sample,dut.padding_sample,
                            expected_row,expected_native,expected_padding,dut.horizontal_phase,dut.vertical_phase);
                    if(dut.sample || dut.fifo_write || dut.awvalid || dut.wvalid)
                        $fatal(1,"disabled capture generated traffic");
                    if(ox==0 && x==0 && y>oy && y<bottom12 && num>1)
                        origin_zero_phase_edges=origin_zero_phase_edges+1;
                    coordinates=coordinates+1;
                end
            end
        end
    endtask

    initial begin
        repeat(8)@(negedge clk);resetn=1;
        repeat(8)@(negedge pixel_clk);
        for(integer ratio=0;ratio<7;ratio=ratio+1)begin
            for(integer origin=0;origin<2;origin=origin+1)begin
                for(integer release_together=0;release_together<2;release_together=release_together+1)begin
                    configure(origin==0 ? 0 : 3,origin==0 ? 0 : 2,
                        WIDTH*(ratio==6 ? 3 : ratio+1)/(ratio==6 ? 2 : 1),
                        HEIGHT*(ratio==6 ? 3 : ratio+1)/(ratio==6 ? 2 : 1),
                        ratio==6 ? 3 : ratio+1,ratio==6 ? 2 : 1,release_together);
                    raster(ox+vw+PAD+2,oy+vh+2);
                end
            end
        end
        for(integer release_together=0;release_together<2;release_together=release_together+1)begin
            // Native right wraps to six; padding retains the wide sum 4160.
            configure(4090,2,12,8,2,1,release_together);
            raster(4096,12);
            // A wrapped bottom must not select any row, even near 4095.
            configure(3,4090,12,12,2,1,release_together);
            raster(3+12+PAD+2,4096);
        end
        if(cases!=32 || origin_zero_phase_edges==0)$fatal(1,"missing capture geometry coverage");
        $display("PASS capture geometry: %0d configurations; %0d coordinate checks; 3-cycle release, frozen inputs, wrapped bounds",cases,coordinates);
        $display("Preserved original row-phase behavior at %0d x=0 scaled row boundaries",origin_zero_phase_edges);
        $finish;
    end
    initial begin #10000000;$fatal(1,"capture geometry timeout");end
endmodule

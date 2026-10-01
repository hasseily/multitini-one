// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_c64_view_bounds;
reg clk=0,resetn=0;
always #5 clk=~clk;
reg [11:0] vx=0,vy=0,vw=0,vh=0,x=0,y=0;
multitini_c64_video_bridge #(.DYNAMIC_DISPLAY(1),.SOURCE_WIDTH(8),.SOURCE_HEIGHT(4)) dut(
    .source_clk(clk),.source_resetn(1'b0),.source_ready(1'b0),
    .source_r(8'd0),.source_g(8'd0),.source_b(8'd0),.source_de(1'b0),.source_vsync(1'b0),.source_valid(1'b0),
    .pixel_clk(clk),.pixel_resetn(resetn),.diagnostic_request(1'b0),
    .display_view_x(vx),.display_view_y(vy),.display_view_width(vw),.display_view_height(vh),
    .display_scale_num(4'd2),.display_scale_den(4'd1),.scan_x(x),.scan_y(y),.vblank_start(1'b0));
integer count=0, c, k;
integer unsigned right12,bottom12,left32,right32;
reg expected_output,expected_prefetch;
task check;
begin
    #1;
    right12=(int'(vx)+int'(vw))%4096;
    bottom12=(int'(vy)+int'(vh))%4096;
    left32=int'(vx)-2;
    right32=int'(vx)+int'(vw)-2;
    expected_output=int'(y)>=int'(vy) && int'(y)<bottom12 && int'(x)>=int'(vx) && int'(x)<right12;
    expected_prefetch=int'(y)>=int'(vy) && int'(y)<bottom12 && int'(x)>=left32 && int'(x)<right32;
    if(dut.output_active!==expected_output || dut.prefetch_active!==expected_prefetch)
        $fatal(1,"geometry mismatch cfg%0d x%0d y%0d",c,x,y);
    count=count+1;
end
endtask
initial begin
    for(c=0;c<64;c=c+1)begin
        @(negedge clk); resetn=0;
        case(c)
        0:begin vx=0;vy=0;vw=0;vh=0;end
        1:begin vx=1;vy=1;vw=4095;vh=4095;end
        2:begin vx=2;vy=4095;vw=4095;vh=2;end
        3:begin vx=4095;vy=0;vw=2;vh=4095;end
        default:begin vx=12'(c*67);vy=12'(c*97);vw=12'(c*131);vh=12'(c*179);end
        endcase
        @(negedge clk); resetn=1;
        for(k=0;k<4096;k=k+1)begin x=12'(k);y=vy;check();y=12'(k*37);check();end
        for(k=0;k<8;k=k+1)begin x=12'(int'(vx)+int'(vw)-k);y=12'(int'(vy)+int'(vh)-k);check();end
    end
    $display("PASS C64 bounds: %0d independent coordinate checks across 64 reset configurations",count);
    $finish;
end
endmodule

// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_video_framebuffer;
    parameter bit SHARED_PIXEL_RESET=0;
    reg clk=0,pixel_clk=0,resetn=0,wr_en=0;
    always #5 clk=~clk;
    always #3.5 pixel_clk=~pixel_clk;
    (* ASYNC_REG="TRUE" *) reg [2:0] shared_release;
    always @(posedge pixel_clk or negedge resetn)
        if(!resetn)shared_release<=0;
        else shared_release<={shared_release[1:0],1'b1};
    wire shared_pixel_resetn=shared_release[2];
    reg [11:0] wr_addr=0,rd_addr=0;
    reg [31:0] wr_data=0;
    reg [3:0] wr_strb=0;
    wire [31:0] rd_data;
    reg [11:0] x=0,y=0;
    wire [31:0] awaddr,araddr;
    wire [3:0] awlen,arlen;
    wire [2:0] awsize,arsize;
    wire [1:0] awburst,arburst;
    wire awvalid,wvalid,wlast,bready,arvalid,rready;
    wire [63:0] wdata;
    wire [7:0] wstrb;
    reg bvalid=0,rvalid=0,rlast=0;
    integer read_remaining=0,reads=0,cycles=0;
    multitini_video_framebuffer #(.WIDTH(6),.HEIGHT(2),.VIEW_X(4),.VIEW_Y(0),.SCALE_NUM(1),
        .SHARED_PIXEL_RESET(SHARED_PIXEL_RESET)) dut(
        .display_view_x(12'd4),.display_view_y(12'd0),.display_view_width(12'd6),.display_view_height(12'd2),
        .display_scale_num(4'd1),.display_scale_den(4'd1),.display_filter_scale_num(4'd1),.display_filter_scale_den(4'd1),
        .capture_display_view_x(12'd4),.capture_display_view_y(12'd0),.capture_display_view_width(12'd6),.capture_display_view_height(12'd2),
        .capture_display_scale_num(4'd1),.capture_display_scale_den(4'd1),
        .display_h_active(12'd128),.display_v_active(12'd8),.display_h_total(12'd128),.display_v_total(12'd8),
        .display_source_x0(10'd0),.display_source_y0(10'd0),.display_phase_x0(4'd0),.display_phase_y0(4'd0),.display_pause(1'b0),
        .clk(clk),.resetn(resetn),.wr_en(wr_en),.wr_addr(wr_addr),.rd_addr(rd_addr),
        .wr_data(wr_data),.wr_strb(wr_strb),.rd_data(rd_data),
        .pixel_clk(pixel_clk),.pixel_resetn(resetn),.vblank_start(x==0 && y==2),
        .shared_pixel_resetn(shared_pixel_resetn),
        .machine_ready(1'b1),.scan_x(x),.scan_y(y),.machine_rgb565(16'h1234),.pixel_use(),.pixel_rgb565(),
        .awaddr(awaddr),.awlen(awlen),.awsize(awsize),.awburst(awburst),.awvalid(awvalid),.awready(1'b1),
        .wdata(wdata),.wstrb(wstrb),.wlast(wlast),.wvalid(wvalid),.wready(1'b1),
        .bresp(2'b00),.bvalid(bvalid),.bready(bready),
        .araddr(araddr),.arlen(arlen),.arsize(arsize),.arburst(arburst),.arvalid(arvalid),.arready(1'b1),
        .rdata(64'h5678567856785678),.rresp(2'b00),.rlast(rlast),.rvalid(rvalid),.rready(rready),
        .fx_awready(1'b1),.fx_wready(1'b1),.fx_bresp(2'b00),.fx_bvalid(1'b0),
        .fx_arready(1'b1),.fx_rdata(64'b0),.fx_rresp(2'b00),.fx_rlast(1'b0),.fx_rvalid(1'b0));
    always @(posedge pixel_clk)begin
        if(!resetn)begin x<=0;y<=0;end
        else if(x==127)begin x<=0;y<=y==7?0:y+1;end
        else x<=x+1;
    end
    always @(posedge clk)begin
        if(wvalid && wlast)bvalid<=1;else if(bvalid && bready)bvalid<=0;
        if(arvalid)begin
            if(read_remaining!=0)$fatal(1,"Overlapping reader bursts");
            read_remaining=arlen+1;reads=reads+1;
        end else if(rvalid && rready)read_remaining=read_remaining-1;
    end
    always @(negedge clk)begin rvalid=read_remaining!=0;rlast=read_remaining==1;end
    task write_reg(input [11:0] a,input [31:0] d,input [3:0] s);
        begin @(negedge clk);wr_en=1;wr_addr=a;wr_data=d;wr_strb=s;@(negedge clk);wr_en=0;end
    endtask
    task expect_reg(input [11:0] a,input [31:0] value);
        reg [31:0] expected;
        begin
            expected=value | (a==12'h184 ? 32'h20 : 32'b0);
            rd_addr=a;#1;if(rd_data!==expected)$fatal(1,"Video transport reg%h got%h expected%h",a,rd_data,expected);
        end
    endtask
    task wait_quiet;
        begin
            rd_addr=12'h184;cycles=0;#1;
            while(!rd_data[3] && cycles<20000)begin @(negedge clk);cycles=cycles+1;end
            if(!rd_data[3])$fatal(1,"Transport failed to quiesce");
            if(awvalid || arvalid || wvalid || rvalid)$fatal(1,"Quiescent with active DDR transaction");
        end
    endtask
    initial begin
        repeat(8)@(negedge clk);resetn=1;
        repeat(30)@(negedge clk);wait_quiet();
        expect_reg(12'h180,0);expect_reg(12'h184,8);expect_reg(12'h188,0);expect_reg(12'h18c,0);
        expect_reg(12'h198,32'h00020006);expect_reg(12'h19c,128);
        expect_reg(12'h1b0,0);expect_reg(12'h1b4,0);expect_reg(12'h1b8,0);
        write_reg(12'h1b0,255,4'hf);expect_reg(12'h1b0,255);
        write_reg(12'h1b0,256,4'hf);expect_reg(12'h1b0,255);expect_reg(12'h184,24);
        write_reg(12'h1ac,1,4'hf);expect_reg(12'h184,8);
        write_reg(12'h1b0,0,4'hf);
        write_reg(12'h180,3,4'h1);expect_reg(12'h180,0);expect_reg(12'h184,24);
        write_reg(12'h1ac,1,4'hf);expect_reg(12'h184,8);
        write_reg(12'h18c,1,4'hf);expect_reg(12'h18c,0);expect_reg(12'h184,24);
        write_reg(12'h1ac,1,4'hf);expect_reg(12'h184,8);
        write_reg(12'h190,7,4'hf);expect_reg(12'h184,24);
        write_reg(12'h1ac,1,4'hf);expect_reg(12'h184,8);
        write_reg(12'h180,2,4'hf);write_reg(12'h190,7,4'hf);
        rd_addr=12'h184;#1;if(!rd_data[1])$fatal(1,"Publication pending bit absent");
        cycles=0;
        while(!rd_data[2] && cycles<20000)begin @(negedge clk);cycles=cycles+1;end
        if(!rd_data[2])$fatal(1,"Output slot3 publication did not activate");
        expect_reg(12'h194,7);
        write_reg(12'h1ac,1,4'hf);rd_addr=12'h184;#1;
        if(!rd_data[4])$fatal(1,"Error clear while enabled accepted");
        repeat(2000)@(negedge clk);
        if(reads==0)$fatal(1,"No DDR reads after display activation");
        write_reg(12'h180,0,4'hf);wait_quiet();
        write_reg(12'h1ac,1,4'hf);expect_reg(12'h184,8);
        expect_reg(12'h1a0,0);expect_reg(12'h1a4,0);expect_reg(12'h1a8,0);
        repeat(500)@(negedge clk);
        if(arvalid || awvalid)$fatal(1,"New DDR request after CONTROL0 quiescence");
        $display("PASS video framebuffer registers: ownership, slot3, byte strobes, disabled-only error clear, quiescence");
        $finish;
    end
    initial begin #2000000;$fatal(1,"Framebuffer register test timeout");end
endmodule

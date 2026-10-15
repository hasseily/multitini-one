// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_apple_iigs_ddr;
    reg core_clk=0,control_clk=0;always #9 core_clk=~core_clk;always #4 control_clk=~control_clk;
    reg core_resetn=0,control_resetn=0;
    reg [23:0] core_memory_addr=0;
    reg core_memory_write=0,core_memory_valid=0;
    reg [63:0] core_memory_wdata=0;
    reg [7:0] core_memory_wstrb=0;
    wire core_memory_ready,core_memory_rvalid,core_memory_fault,quiescent;
    wire [63:0] core_memory_rdata;
    wire [31:0] araddr,awaddr;wire [3:0] arlen,awlen;
    wire [2:0] arsize,awsize;wire [1:0] arburst,awburst;
    wire arvalid,rready,awvalid,wvalid,wlast,bready;
    wire [63:0] wdata;wire [7:0] wstrb;
    reg arready=0,awready=0,wready=0,rvalid=0,rlast=1,bvalid=0;
    reg [63:0] rdata=0;reg [1:0] rresp=0,bresp=0;
    multitini_apple_iigs_ddr_path dut(.*);
    reg [63:0] ram[0:63];
    reg have_aw=0,have_w=0,read_pending=0;
    reg [31:0] aw_saved,ar_saved;
    reg [63:0] w_saved;reg [7:0] strobe_saved;
    integer cycle=0,delay_count=0,writes=0;
    reg inject_error=0;
    reg glitch_window=0;
    always @(negedge control_clk)begin
        if(glitch_window && (!quiescent || dut.core_busy_control))
            $fatal(1,"core busy decode glitch crossed into the drain state");
    end
    always @(posedge control_clk)begin
        cycle<=cycle+1;
        arready<=control_resetn && !read_pending && !rvalid && cycle%4==0;
        awready<=control_resetn && !have_aw && !bvalid && cycle%7==0;
        wready<=control_resetn && !have_w && !bvalid && cycle%3==0;
        if(arvalid && arready)begin
            if(araddr[31:24]!=8'h24 || arlen!=0 || arsize!=3 || arburst!=1)$fatal(1,"read bounds/attributes");
            ar_saved<=araddr;read_pending<=1;delay_count<=13;
        end
        if(read_pending)begin
            if(delay_count!=0)delay_count<=delay_count-1;
            else begin rdata<=ram[ar_saved[8:3]];rresp<=inject_error ? 2 : 0;rvalid<=1;read_pending<=0;end
        end
        if(rvalid && rready)rvalid<=0;
        if(awvalid && awready)begin
            if(awaddr[31:24]!=8'h24 || awlen!=0 || awsize!=3 || awburst!=1)$fatal(1,"write bounds/attributes");
            have_aw<=1;aw_saved<=awaddr;
        end
        if(wvalid && wready)begin if(!wlast)$fatal(1,"write missing WLAST");have_w<=1;w_saved<=wdata;strobe_saved<=wstrb;end
        if(have_aw && have_w && !bvalid)begin
            for(integer i=0;i<8;i=i+1)if(strobe_saved[i])ram[aw_saved[8:3]][i*8+:8]<=w_saved[i*8+:8];
            writes<=writes+1;bvalid<=1;bresp<=0;have_aw<=0;have_w<=0;
        end
        if(bvalid && bready)bvalid<=0;
    end
    task transact(input bit write_op,input [23:0] addr,input [63:0] value,input [7:0] strobe,input [63:0] expected);
        integer n;
        begin
            @(negedge core_clk);core_memory_addr=addr;core_memory_wdata=value;core_memory_wstrb=strobe;
            core_memory_write=write_op;core_memory_valid=1;
            n=0;while(!core_memory_ready && n<1000)begin @(negedge core_clk);n=n+1;end
            if(n==1000)$fatal(1,"request timeout");
            @(negedge core_clk);core_memory_valid=0;
            n=0;while(!core_memory_rvalid && n<1000)begin @(negedge core_clk);n=n+1;if(quiescent)$fatal(1,"premature drain");end
            if(n==1000)$fatal(1,"response timeout");
            if(!write_op && core_memory_rdata!==expected)$fatal(1,"DDR returned %h expected %h",core_memory_rdata,expected);
            @(negedge core_clk);
        end
    endtask
    initial begin
        for(integer i=0;i<64;i=i+1)ram[i]=0;
        repeat(5)@(negedge core_clk);control_resetn=1;core_resetn=1;
        repeat(20)@(negedge core_clk);
        if(!quiescent)$fatal(1,"initial drain");
        // Model a decode pulse that spans a control edge but ends before
        // the next source edge. A registered source must suppress it.
        @(negedge core_clk);glitch_window=1;
        #1;force dut.cdc_core_busy=1'b1;
        #7.5;release dut.cdc_core_busy;
        repeat(4)@(negedge control_clk);
        if(!quiescent)$fatal(1,"busy glitch interrupted complete drain");
        glitch_window=0;
        transact(1,24'hfc0100,64'h8877665544332211,8'hff,0);
        transact(1,24'hfc0100,64'h0000000000aa0000,8'h04,0);
        transact(0,24'hfc0100,0,0,64'h8877665544aa2211);
        repeat(25)@(negedge core_clk);
        if(!quiescent || writes!=2 || core_memory_fault)$fatal(1,"completion/drain/writes");
        inject_error=1;transact(0,24'hfc0100,0,0,64'h8877665544aa2211);
        repeat(10)@(negedge core_clk);if(!core_memory_fault)$fatal(1,"AXI error not surfaced");
        $display("PASS: IIgs DDR mailbox, independent AW/W backpressure, strobe lanes, address aperture, registered busy glitch suppression, complete drain and AXI error");$finish;
    end
    initial begin #500000;$fatal(1,"timeout");end
endmodule

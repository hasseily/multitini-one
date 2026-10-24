// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_c64_uci_axi;
    reg clk=0,core_clk=0;always #5 clk=~clk;always #7 core_clk=~core_clk;
    reg resetn=0,awvalid=0,wvalid=0,arvalid=0,rready=1;
    reg [31:0] awaddr=0,wdata=0,araddr=0;reg [3:0] wstrb=15;
    wire awready,wready,bvalid,arready_raw,rvalid;wire [31:0] rdata;wire [1:0] bresp,rresp;
    wire reg_wr_en,reg_rd_en,early;wire [15:0] reg_wr_addr,reg_rd_addr,early_addr;
    wire [31:0] reg_wr_data,ram_data;wire [3:0] reg_wr_strb;
    wire select_write=reg_wr_addr>=16'hc000 && reg_wr_addr<16'hc800;
    wire free_port=!(reg_wr_en && select_write);
    wire arready=arready_raw && free_port;
    integer checks=0,waits;
    multitini_axi3_regs axi(.aclk(clk),.aresetn(resetn),
        .s_axi_awid(12'd0),.s_axi_awaddr(awaddr),.s_axi_awlen(4'd0),.s_axi_awsize(3'd2),
        .s_axi_awburst(2'd1),.s_axi_awlock(2'd0),.s_axi_awcache(4'd0),.s_axi_awprot(3'd0),.s_axi_awqos(4'd0),
        .s_axi_awvalid(awvalid),.s_axi_awready(awready),.s_axi_wid(12'd0),.s_axi_wdata(wdata),.s_axi_wstrb(wstrb),
        .s_axi_wlast(1'b1),.s_axi_wvalid(wvalid),.s_axi_wready(wready),.s_axi_bid(),.s_axi_bresp(bresp),.s_axi_bvalid(bvalid),.s_axi_bready(1'b1),
        .s_axi_arid(12'd0),.s_axi_araddr(araddr),.s_axi_arlen(4'd0),.s_axi_arsize(3'd2),.s_axi_arburst(2'd1),
        .s_axi_arlock(2'd0),.s_axi_arcache(4'd0),.s_axi_arprot(3'd0),.s_axi_arqos(4'd0),
        .s_axi_arvalid(arvalid && free_port),.s_axi_arready(arready_raw),.s_axi_rid(),.s_axi_rdata(rdata),.s_axi_rresp(rresp),
        .s_axi_rlast(),.s_axi_rvalid(rvalid),.s_axi_rready(rready),
        .reg_wr_en(reg_wr_en),.reg_wr_addr(reg_wr_addr),.reg_wr_data(reg_wr_data),.reg_wr_strb(reg_wr_strb),
        .reg_rd_en(reg_rd_en),.reg_rd_addr(reg_rd_addr),.reg_rd_early_en(early),.reg_rd_early_addr(early_addr),.reg_rd_data(ram_data));
    multitini_c64_uci_transport transport(.host_clk(clk),.host_resetn(resetn),.host_op(32'd0),.host_op_valid(1'b0),.host_op_ready(),
        .host_read_addr(early?early_addr[10:2]:reg_rd_addr[10:2]),.host_write_addr(reg_wr_addr[10:2]),
        .host_write_data(reg_wr_data),.host_write_strobe(reg_wr_en && select_write?reg_wr_strb:4'd0),.host_read_data(ram_data),
        .core_clk(core_clk),.core_resetn(resetn),.core_op(),.core_op_valid(),.core_op_ready(1'b1),
        .core_addr(11'd0),.core_write(1'b0),.core_write_data(8'd0),.core_read_data());
    task tick(input integer n);repeat(n)begin @(posedge clk);#1;end endtask
    task check(input bit ok,input string why);checks++;if(!ok)$fatal(1,"UCI AXI %s",why);endtask
    task write_word(input [31:0] address,input [31:0] value,input [3:0] lanes,input bit bad);
        @(negedge clk);awaddr=address;wdata=value;wstrb=lanes;awvalid=1;wvalid=1;
        tick(1);@(negedge clk);awvalid=0;wvalid=0;
        check(bvalid && bresp==(bad?2:0),"write response");tick(2);
    endtask
    task read_word(input [31:0] address,input [31:0] expected,input bit bad);
        @(negedge clk);araddr=address;arvalid=1;rready=0;
        do begin @(posedge clk);end while(!arready);
        @(negedge clk);arvalid=0;araddr=32'hdeadb000;
        waits=0;while(!rvalid && waits<10)begin tick(1);waits++;end
        check(rvalid && rresp==(bad?2:0),"read response after latched address");
        if(!bad)check(rdata===expected,"read prefetch is accepted address, not live address");
        tick(3);if(!bad)check(rdata===expected,"stalled read holds value");
        rready=1;tick(2);
    endtask
    initial begin
        tick(20);resetn=1;tick(30);
        write_word('hc000,32'h12345678,15,0);write_word('hc004,32'haabbccdd,15,0);
        read_word('hc000,32'h12345678,0);read_word('hc004,32'haabbccdd,0);
        write_word('hc004,32'h00110022,5,0);read_word('hc004,32'haa11cc22,0);
        write_word('hc001,32'hffffffff,15,1);read_word('hc001,0,1);read_word('hc000,32'h12345678,0);
        // AR arrives on the cycle that a prior accepted AXI write owns BRAM.
        @(negedge clk);awaddr='hc004;wdata=32'h55667788;wstrb=15;awvalid=1;wvalid=1;tick(1);
        @(negedge clk);awvalid=0;wvalid=0;araddr='hc000;arvalid=1;#1;
        check(!arready,"BRAM collision backpressures read acceptance");
        do @(posedge clk);while(!arready);
        @(negedge clk);arvalid=0;araddr='hc004;tick(1);
        check(rvalid && rdata===32'h12345678,"read after concurrent write uses correct word");tick(2);
        read_word('hc004,32'h55667788,0);
        $display("PASS C64 UCI AXI checks=%0d",checks);$finish;
    end
    initial begin #200000;$fatal(1,"UCI AXI timeout");end
endmodule

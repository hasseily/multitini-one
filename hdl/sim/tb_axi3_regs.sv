// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

module tb_axi3_regs;
    logic clk = 1'b0;
    logic resetn = 1'b0;
    logic [11:0] awid = 12'd0;
    logic [31:0] awaddr = 32'd0;
    logic [3:0] awlen = 4'd0;
    logic [2:0] awsize = 3'b010;
    logic [1:0] awburst = 2'b01;
    logic [1:0] awlock = 2'd0;
    logic [3:0] awcache = 4'd0;
    logic [2:0] awprot = 3'd0;
    logic [3:0] awqos = 4'd0;
    logic awvalid = 1'b0;
    logic awready;
    logic [11:0] wid = 12'd0;
    logic [31:0] wdata = 32'd0;
    logic [3:0] wstrb = 4'hF;
    logic wlast = 1'b1;
    logic wvalid = 1'b0;
    logic wready;
    logic [11:0] bid;
    logic [1:0] bresp;
    logic bvalid;
    logic bready = 1'b1;
    logic [11:0] arid = 12'd0;
    logic [31:0] araddr = 32'd0;
    logic [3:0] arlen = 4'd0;
    logic [2:0] arsize = 3'b010;
    logic [1:0] arburst = 2'b01;
    logic [1:0] arlock = 2'd0;
    logic [3:0] arcache = 4'd0;
    logic [2:0] arprot = 3'd0;
    logic [3:0] arqos = 4'd0;
    logic arvalid = 1'b0;
    logic arready;
    logic [11:0] rid;
    logic [31:0] rdata;
    logic [1:0] rresp;
    logic rlast;
    logic rvalid;
    logic rready = 1'b1;
    logic reg_wr_en;
    logic [11:0] reg_wr_addr;
    logic [31:0] reg_wr_data;
    logic [3:0] reg_wr_strb;
    logic reg_rd_en;
    logic [11:0] reg_rd_addr;
    logic [31:0] reg_rd_data;
    integer write_count = 0;

    always #5 clk = !clk;
    always_comb reg_rd_data = 32'hA0000000 | {20'd0, reg_rd_addr};
    always @(posedge clk) if (reg_wr_en) write_count <= write_count + 1;

    multitini_axi3_regs dut (
        .aclk(clk), .aresetn(resetn), .s_axi_awid(awid),
        .s_axi_awaddr(awaddr), .s_axi_awlen(awlen), .s_axi_awsize(awsize),
        .s_axi_awburst(awburst), .s_axi_awlock(awlock),
        .s_axi_awcache(awcache), .s_axi_awprot(awprot),
        .s_axi_awqos(awqos), .s_axi_awvalid(awvalid),
        .s_axi_awready(awready), .s_axi_wid(wid), .s_axi_wdata(wdata),
        .s_axi_wstrb(wstrb), .s_axi_wlast(wlast), .s_axi_wvalid(wvalid),
        .s_axi_wready(wready), .s_axi_bid(bid), .s_axi_bresp(bresp),
        .s_axi_bvalid(bvalid), .s_axi_bready(bready), .s_axi_arid(arid),
        .s_axi_araddr(araddr), .s_axi_arlen(arlen), .s_axi_arsize(arsize),
        .s_axi_arburst(arburst), .s_axi_arlock(arlock),
        .s_axi_arcache(arcache), .s_axi_arprot(arprot),
        .s_axi_arqos(arqos), .s_axi_arvalid(arvalid),
        .s_axi_arready(arready), .s_axi_rid(rid), .s_axi_rdata(rdata),
        .s_axi_rresp(rresp), .s_axi_rlast(rlast), .s_axi_rvalid(rvalid),
        .s_axi_rready(rready), .reg_wr_en(reg_wr_en),
        .reg_wr_addr(reg_wr_addr), .reg_wr_data(reg_wr_data),
        .reg_wr_strb(reg_wr_strb), .reg_rd_en(reg_rd_en),
        .reg_rd_addr(reg_rd_addr), .reg_rd_data(reg_rd_data)
    );

    task automatic check_value(input logic condition, input string message);
        if (!condition) begin
            $display("FAIL: %s", message);
            $fatal(1);
        end
    endtask

    task automatic send_aw(input logic [11:0] id, input logic [31:0] address,
                           input logic [2:0] size);
        @(negedge clk);
        awid = id; awaddr = address; awsize = size; awvalid = 1'b1;
        do @(posedge clk); while (!awready);
        @(negedge clk);
        awvalid = 1'b0;
    endtask

    task automatic send_w(input logic [11:0] id, input logic [31:0] data);
        @(negedge clk);
        wid = id; wdata = data; wvalid = 1'b1;
        do @(posedge clk); while (!wready);
        @(negedge clk);
        wvalid = 1'b0;
    endtask

    initial begin
        repeat (3) @(negedge clk);
        resetn = 1'b1;

        send_aw(12'h123, 32'h40000028, 3'b010);
        repeat (2) @(negedge clk);
        send_w(12'h123, 32'hDEADBEEF);
        wait (bvalid);
        #1;
        check_value(bresp == 2'b00 && bid == 12'h123, "valid write response");
        check_value(reg_wr_addr == 12'h028 && reg_wr_data == 32'hDEADBEEF,
               "write payload");
        @(negedge clk);
        check_value(write_count == 1, "one accepted register write");

        send_aw(12'h124, 32'h40000028, 3'b001);
        send_w(12'h124, 32'h11111111);
        wait (bvalid);
        #1;
        check_value(bresp == 2'b10, "narrow write rejected");
        @(negedge clk);
        check_value(write_count == 1, "rejected write did not reach bank");

        @(negedge clk);
        arid = 12'h321; araddr = 32'h40000014; arvalid = 1'b1;
        do @(posedge clk); while (!arready);
        @(negedge clk);
        arvalid = 1'b0;
        wait (rvalid);
        #1;
        check_value(rresp == 2'b00 && rid == 12'h321 && rlast,
               "valid read response");
        check_value(rdata == 32'hA0000014, "read data");

        @(negedge clk);
        arsize = 3'b001; araddr = 32'h40000018; arvalid = 1'b1;
        do @(posedge clk); while (!arready);
        @(negedge clk);
        arvalid = 1'b0;
        wait (rvalid);
        #1;
        check_value(rresp == 2'b10, "narrow read rejected");

        $display("PASS: AXI3 register adapter");
        $finish;
    end
endmodule

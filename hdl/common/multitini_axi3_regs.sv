// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// Single-beat AXI3 slave used for the PS GP0 register window. Address and data
// channels are buffered independently. Bursts and non-word transfers return
// SLVERR and do not reach the register bank.
module multitini_axi3_regs (
    input  logic         aclk,
    input  logic         aresetn,

    input  logic [11:0]  s_axi_awid,
    input  logic [31:0]  s_axi_awaddr,
    input  logic [3:0]   s_axi_awlen,
    input  logic [2:0]   s_axi_awsize,
    input  logic [1:0]   s_axi_awburst,
    input  logic [1:0]   s_axi_awlock,
    input  logic [3:0]   s_axi_awcache,
    input  logic [2:0]   s_axi_awprot,
    input  logic [3:0]   s_axi_awqos,
    input  logic         s_axi_awvalid,
    output logic         s_axi_awready,

    input  logic [11:0]  s_axi_wid,
    input  logic [31:0]  s_axi_wdata,
    input  logic [3:0]   s_axi_wstrb,
    input  logic         s_axi_wlast,
    input  logic         s_axi_wvalid,
    output logic         s_axi_wready,

    output logic [11:0]  s_axi_bid,
    output logic [1:0]   s_axi_bresp,
    output logic         s_axi_bvalid,
    input  logic         s_axi_bready,

    input  logic [11:0]  s_axi_arid,
    input  logic [31:0]  s_axi_araddr,
    input  logic [3:0]   s_axi_arlen,
    input  logic [2:0]   s_axi_arsize,
    input  logic [1:0]   s_axi_arburst,
    input  logic [1:0]   s_axi_arlock,
    input  logic [3:0]   s_axi_arcache,
    input  logic [2:0]   s_axi_arprot,
    input  logic [3:0]   s_axi_arqos,
    input  logic         s_axi_arvalid,
    output logic         s_axi_arready,

    output logic [11:0]  s_axi_rid,
    output logic [31:0]  s_axi_rdata,
    output logic [1:0]   s_axi_rresp,
    output logic         s_axi_rlast,
    output logic         s_axi_rvalid,
    input  logic         s_axi_rready,

    output logic         reg_wr_en,
    output logic [15:0]  reg_wr_addr,
    output logic [31:0]  reg_wr_data,
    output logic [3:0]   reg_wr_strb,
    output logic         reg_rd_en,
    output logic [15:0]  reg_rd_addr,
    input  logic [31:0]  reg_rd_data
);

    localparam logic [1:0] AXI_OKAY   = 2'b00;
    localparam logic [1:0] AXI_SLVERR = 2'b10;

    logic        aw_hold;
    logic [11:0] awid_q;
    logic [31:0] awaddr_q;
    logic [3:0]  awlen_q;
    logic [2:0]  awsize_q;
    logic        w_hold;
    logic [11:0] wid_q;
    logic [31:0] wdata_q;
    logic [3:0]  wstrb_q;
    logic        wlast_q;

    wire aw_take = s_axi_awvalid && s_axi_awready;
    wire w_take  = s_axi_wvalid && s_axi_wready;
    wire ar_take = s_axi_arvalid && s_axi_arready;
    wire have_aw = aw_hold || aw_take;
    wire have_w  = w_hold || w_take;

    wire [11:0] selected_awid   = aw_hold ? awid_q   : s_axi_awid;
    wire [31:0] selected_awaddr = aw_hold ? awaddr_q : s_axi_awaddr;
    wire [3:0]  selected_awlen  = aw_hold ? awlen_q  : s_axi_awlen;
    wire [2:0]  selected_awsize = aw_hold ? awsize_q : s_axi_awsize;
    wire [11:0] selected_wid    = w_hold ? wid_q    : s_axi_wid;
    wire [31:0] selected_wdata  = w_hold ? wdata_q  : s_axi_wdata;
    wire [3:0]  selected_wstrb  = w_hold ? wstrb_q  : s_axi_wstrb;
    wire        selected_wlast  = w_hold ? wlast_q  : s_axi_wlast;

    assign s_axi_awready = !aw_hold && !s_axi_bvalid;
    assign s_axi_wready  = !w_hold && !s_axi_bvalid;
    assign s_axi_arready = !s_axi_rvalid;
    assign reg_rd_addr   = s_axi_araddr[15:0];

    // Attributes are intentionally accepted but have no effect in this
    // device-register slave.
    wire unused_attributes = ^{s_axi_awburst, s_axi_awlock, s_axi_awcache,
                               s_axi_awprot, s_axi_awqos, s_axi_arburst,
                               s_axi_arlock, s_axi_arcache, s_axi_arprot,
                               s_axi_arqos};

    always_ff @(posedge aclk) begin
        if (!aresetn) begin
            aw_hold      <= 1'b0;
            awid_q       <= '0;
            awaddr_q     <= '0;
            awlen_q      <= '0;
            awsize_q     <= '0;
            w_hold       <= 1'b0;
            wid_q        <= '0;
            wdata_q      <= '0;
            wstrb_q      <= '0;
            wlast_q      <= 1'b0;
            s_axi_bid    <= '0;
            s_axi_bresp  <= AXI_OKAY;
            s_axi_bvalid <= 1'b0;
            s_axi_rid    <= '0;
            s_axi_rdata  <= '0;
            s_axi_rresp  <= AXI_OKAY;
            s_axi_rlast  <= 1'b0;
            s_axi_rvalid <= 1'b0;
            reg_wr_en    <= 1'b0;
            reg_wr_addr  <= '0;
            reg_wr_data  <= '0;
            reg_wr_strb  <= '0;
            reg_rd_en    <= 1'b0;
        end else begin
            reg_wr_en <= 1'b0;
            reg_rd_en <= 1'b0;

            if (s_axi_bvalid && s_axi_bready)
                s_axi_bvalid <= 1'b0;
            if (s_axi_rvalid && s_axi_rready)
                s_axi_rvalid <= 1'b0;

            if (aw_take) begin
                aw_hold  <= 1'b1;
                awid_q   <= s_axi_awid;
                awaddr_q <= s_axi_awaddr;
                awlen_q  <= s_axi_awlen;
                awsize_q <= s_axi_awsize;
            end
            if (w_take) begin
                w_hold  <= 1'b1;
                wid_q   <= s_axi_wid;
                wdata_q <= s_axi_wdata;
                wstrb_q <= s_axi_wstrb;
                wlast_q <= s_axi_wlast;
            end

            if (!s_axi_bvalid && have_aw && have_w) begin
                aw_hold      <= 1'b0;
                w_hold       <= 1'b0;
                s_axi_bid    <= selected_awid;
                s_axi_bvalid <= 1'b1;
                if (selected_awlen == 4'd0 && selected_awsize == 3'b010 &&
                    selected_wlast && selected_awid == selected_wid) begin
                    s_axi_bresp <= AXI_OKAY;
                    reg_wr_en   <= 1'b1;
                    reg_wr_addr <= selected_awaddr[15:0];
                    reg_wr_data <= selected_wdata;
                    reg_wr_strb <= selected_wstrb;
                end else begin
                    s_axi_bresp <= AXI_SLVERR;
                end
            end

            if (ar_take) begin
                s_axi_rid    <= s_axi_arid;
                s_axi_rdata  <= reg_rd_data;
                s_axi_rresp  <= (s_axi_arlen == 4'd0 && s_axi_arsize == 3'b010)
                                ? AXI_OKAY : AXI_SLVERR;
                s_axi_rlast  <= 1'b1;
                s_axi_rvalid <= 1'b1;
                reg_rd_en    <= s_axi_arlen == 4'd0 && s_axi_arsize == 3'b010;
            end
        end
    end

endmodule

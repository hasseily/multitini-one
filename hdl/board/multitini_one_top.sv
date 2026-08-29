// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

module multitini_one_top #(
    parameter logic [31:0] SHELL_BUILD_ID = 32'h00000000
) (
    inout  wire [53:0] MIO,
    inout  wire        PS_CLK,
    inout  wire        PS_PORB,
    inout  wire        PS_SRSTB,
    inout  wire        DDR_VRP,
    inout  wire        DDR_VRN,
    inout  wire [14:0] DDR_Addr,
    inout  wire [2:0]  DDR_BankAddr,
    inout  wire        DDR_CAS_n,
    inout  wire        DDR_Clk,
    inout  wire        DDR_Clk_n,
    inout  wire        DDR_CKE,
    inout  wire        DDR_CS_n,
    inout  wire [3:0]  DDR_DM,
    inout  wire [31:0] DDR_DQ,
    inout  wire [3:0]  DDR_DQS,
    inout  wire [3:0]  DDR_DQS_n,
    inout  wire        DDR_ODT,
    inout  wire        DDR_RAS_n,
    inout  wire        DDR_DRSTB,
    inout  wire        DDR_WEB,

    output logic       a2ctrl_irq_n,
    output logic       a2ctrl_nmi_n,
    output logic       a2ctrl_rdy_n,
    output logic       a2ctrl_dma_n,
    output logic       a2ctrl_inh_n,
    output logic       a2ctrl_reset_n,
    inout  wire        a2fpga_dma_n,
    inout  wire        a2fpga_rdy_n,
    inout  wire        a2fpga_inh_n,
    inout  wire        a2fpga_nmi_n,
    inout  wire        a2fpga_reset_n,
    inout  wire        a2fpga_irq_n,
    output logic       a2fpga_oe_n,
    output logic       a2fpga_oe_n_aux,
    output logic       a2fpga_dir_a,
    output logic       a2fpga_dir_d,
    inout  wire [15:0] a2fpga_a,
    inout  wire [7:0]  a2fpga_d,
    inout  wire        a2fpga_rdwr_n,
    input  logic       a2fpga_clk,
    input  logic       a2fpga_7m,
    input  logic       a2fpga_q3,
    input  logic       a2fpga_m2b0,
    input  logic       a2fpga_m2sel,
    input  logic       a2fpga_devsel_n,

    output logic [4:0] dvi_red,
    output logic [5:0] dvi_grn,
    output logic [4:0] dvi_blu,
    output logic       dvi_clk,
    output logic       dvi_de,
    output logic       dvi_hsync,
    output logic       dvi_vsync,
    inout  wire        dvi_scl,
    inout  wire        dvi_sda,
    input  logic       dvi_hpd,

    input  logic       audio_i2s_mclk,
    output logic       audio_i2s_lrck,
    output logic       audio_i2s_sdata,
    output logic       audio_i2s_bick,
    output logic       audio_spdif_out,

    inout  wire [3:0]  psram_a_io,
    inout  wire [3:0]  psram_b_io,
    output logic       psram_ce_n,
    output logic       psram_clk,

    input  logic       i2c0_rtc_int_n,
    input  logic       i2c0_rtc_clkout,
    input  logic       i2c0_temp_alert_n,

    inout  wire [7:0]  eth_d,
    output logic [1:0] eth_a,
    output logic       eth_rd_n,
    output logic       eth_wr_n,
    output logic       eth_cs_n,
    output logic       eth_rst_n,
    input  logic       eth_int_n,
    input  logic       usb_sys_fault_n
);
    import multitini_regs_pkg::*;

    logic control_clk;
    logic idelay_ref_clk;
    logic video_clk;
    logic control_aresetn;
    logic video_aresetn;
    logic [15:0] irq_bus;
    logic shell_irq;
    logic [31:0] shell_control;

    logic [31:0] m_axi_araddr;
    logic [1:0]  m_axi_arburst;
    logic [3:0]  m_axi_arcache;
    logic [11:0] m_axi_arid;
    logic [3:0]  m_axi_arlen;
    logic [1:0]  m_axi_arlock;
    logic [2:0]  m_axi_arprot;
    logic [3:0]  m_axi_arqos;
    logic        m_axi_arready;
    logic [2:0]  m_axi_arsize;
    logic        m_axi_arvalid;
    logic [31:0] m_axi_awaddr;
    logic [1:0]  m_axi_awburst;
    logic [3:0]  m_axi_awcache;
    logic [11:0] m_axi_awid;
    logic [3:0]  m_axi_awlen;
    logic [1:0]  m_axi_awlock;
    logic [2:0]  m_axi_awprot;
    logic [3:0]  m_axi_awqos;
    logic        m_axi_awready;
    logic [2:0]  m_axi_awsize;
    logic        m_axi_awvalid;
    logic [11:0] m_axi_bid;
    logic        m_axi_bready;
    logic [1:0]  m_axi_bresp;
    logic        m_axi_bvalid;
    logic [31:0] m_axi_rdata;
    logic [11:0] m_axi_rid;
    logic        m_axi_rlast;
    logic        m_axi_rready;
    logic [1:0]  m_axi_rresp;
    logic        m_axi_rvalid;
    logic [31:0] m_axi_wdata;
    logic [11:0] m_axi_wid;
    logic        m_axi_wlast;
    logic        m_axi_wready;
    logic [3:0]  m_axi_wstrb;
    logic        m_axi_wvalid;

    logic        reg_wr_en;
    logic [11:0] reg_wr_addr;
    logic [31:0] reg_wr_data;
    logic [3:0]  reg_wr_strb;
    logic        reg_rd_en;
    logic [11:0] reg_rd_addr;
    logic [31:0] reg_rd_data;
    logic        video_enable;

    assign irq_bus = {15'd0, shell_irq};

    zynq_ps_bd_wrapper zynq_ps_i (
        .DDR_addr(DDR_Addr), .DDR_ba(DDR_BankAddr),
        .DDR_cas_n(DDR_CAS_n), .DDR_ck_n(DDR_Clk_n), .DDR_ck_p(DDR_Clk),
        .DDR_cke(DDR_CKE), .DDR_cs_n(DDR_CS_n), .DDR_dm(DDR_DM),
        .DDR_dq(DDR_DQ), .DDR_dqs_n(DDR_DQS_n), .DDR_dqs_p(DDR_DQS),
        .DDR_odt(DDR_ODT), .DDR_ras_n(DDR_RAS_n),
        .DDR_reset_n(DDR_DRSTB), .DDR_we_n(DDR_WEB),
        .FIXED_IO_ddr_vrn(DDR_VRN), .FIXED_IO_ddr_vrp(DDR_VRP),
        .FIXED_IO_mio(MIO), .FIXED_IO_ps_clk(PS_CLK),
        .FIXED_IO_ps_porb(PS_PORB), .FIXED_IO_ps_srstb(PS_SRSTB),

        .M_AXI_GP0_0_araddr(m_axi_araddr), .M_AXI_GP0_0_arburst(m_axi_arburst),
        .M_AXI_GP0_0_arcache(m_axi_arcache), .M_AXI_GP0_0_arid(m_axi_arid),
        .M_AXI_GP0_0_arlen(m_axi_arlen), .M_AXI_GP0_0_arlock(m_axi_arlock),
        .M_AXI_GP0_0_arprot(m_axi_arprot), .M_AXI_GP0_0_arqos(m_axi_arqos),
        .M_AXI_GP0_0_arready(m_axi_arready), .M_AXI_GP0_0_arsize(m_axi_arsize),
        .M_AXI_GP0_0_arvalid(m_axi_arvalid),
        .M_AXI_GP0_0_awaddr(m_axi_awaddr), .M_AXI_GP0_0_awburst(m_axi_awburst),
        .M_AXI_GP0_0_awcache(m_axi_awcache), .M_AXI_GP0_0_awid(m_axi_awid),
        .M_AXI_GP0_0_awlen(m_axi_awlen), .M_AXI_GP0_0_awlock(m_axi_awlock),
        .M_AXI_GP0_0_awprot(m_axi_awprot), .M_AXI_GP0_0_awqos(m_axi_awqos),
        .M_AXI_GP0_0_awready(m_axi_awready), .M_AXI_GP0_0_awsize(m_axi_awsize),
        .M_AXI_GP0_0_awvalid(m_axi_awvalid),
        .M_AXI_GP0_0_bid(m_axi_bid), .M_AXI_GP0_0_bready(m_axi_bready),
        .M_AXI_GP0_0_bresp(m_axi_bresp), .M_AXI_GP0_0_bvalid(m_axi_bvalid),
        .M_AXI_GP0_0_rdata(m_axi_rdata), .M_AXI_GP0_0_rid(m_axi_rid),
        .M_AXI_GP0_0_rlast(m_axi_rlast), .M_AXI_GP0_0_rready(m_axi_rready),
        .M_AXI_GP0_0_rresp(m_axi_rresp), .M_AXI_GP0_0_rvalid(m_axi_rvalid),
        .M_AXI_GP0_0_wdata(m_axi_wdata), .M_AXI_GP0_0_wid(m_axi_wid),
        .M_AXI_GP0_0_wlast(m_axi_wlast), .M_AXI_GP0_0_wready(m_axi_wready),
        .M_AXI_GP0_0_wstrb(m_axi_wstrb), .M_AXI_GP0_0_wvalid(m_axi_wvalid),

        .control_clk(control_clk), .idelay_ref_clk(idelay_ref_clk),
        .video_clk(video_clk), .control_aresetn(control_aresetn),
        .video_aresetn(video_aresetn), .IRQ(irq_bus)
    );

    multitini_axi3_regs axi_regs (
        .aclk(control_clk), .aresetn(control_aresetn),
        .s_axi_awid(m_axi_awid), .s_axi_awaddr(m_axi_awaddr),
        .s_axi_awlen(m_axi_awlen), .s_axi_awsize(m_axi_awsize),
        .s_axi_awburst(m_axi_awburst), .s_axi_awlock(m_axi_awlock),
        .s_axi_awcache(m_axi_awcache), .s_axi_awprot(m_axi_awprot),
        .s_axi_awqos(m_axi_awqos), .s_axi_awvalid(m_axi_awvalid),
        .s_axi_awready(m_axi_awready), .s_axi_wid(m_axi_wid),
        .s_axi_wdata(m_axi_wdata), .s_axi_wstrb(m_axi_wstrb),
        .s_axi_wlast(m_axi_wlast), .s_axi_wvalid(m_axi_wvalid),
        .s_axi_wready(m_axi_wready), .s_axi_bid(m_axi_bid),
        .s_axi_bresp(m_axi_bresp), .s_axi_bvalid(m_axi_bvalid),
        .s_axi_bready(m_axi_bready), .s_axi_arid(m_axi_arid),
        .s_axi_araddr(m_axi_araddr), .s_axi_arlen(m_axi_arlen),
        .s_axi_arsize(m_axi_arsize), .s_axi_arburst(m_axi_arburst),
        .s_axi_arlock(m_axi_arlock), .s_axi_arcache(m_axi_arcache),
        .s_axi_arprot(m_axi_arprot), .s_axi_arqos(m_axi_arqos),
        .s_axi_arvalid(m_axi_arvalid), .s_axi_arready(m_axi_arready),
        .s_axi_rid(m_axi_rid), .s_axi_rdata(m_axi_rdata),
        .s_axi_rresp(m_axi_rresp), .s_axi_rlast(m_axi_rlast),
        .s_axi_rvalid(m_axi_rvalid), .s_axi_rready(m_axi_rready),
        .reg_wr_en(reg_wr_en), .reg_wr_addr(reg_wr_addr),
        .reg_wr_data(reg_wr_data), .reg_wr_strb(reg_wr_strb),
        .reg_rd_en(reg_rd_en), .reg_rd_addr(reg_rd_addr),
        .reg_rd_data(reg_rd_data)
    );

    multitini_control_regs #(.SHELL_BUILD_ID(SHELL_BUILD_ID)) control_regs (
        .clk(control_clk), .resetn(control_aresetn),
        .core_id(32'h00000000), .core_ready(1'b0),
        .wr_en(reg_wr_en), .wr_addr(reg_wr_addr), .wr_data(reg_wr_data),
        .wr_strb(reg_wr_strb), .rd_en(reg_rd_en), .rd_addr(reg_rd_addr),
        .rd_data(reg_rd_data), .control(shell_control), .irq(shell_irq)
    );

    cdc_bit_sync video_enable_sync (
        .clk(video_clk), .resetn(video_aresetn),
        .din(|(shell_control &
               multitini_regs_pkg::MT_CONTROL_VIDEO_TEST_ENABLE)),
        .dout(video_enable)
    );

    multitini_video_test video_test (
        .pixel_clk(video_clk), .resetn(video_aresetn), .enable(video_enable),
        .dvi_red(dvi_red), .dvi_grn(dvi_grn), .dvi_blu(dvi_blu),
        .dvi_clk(dvi_clk), .dvi_de(dvi_de),
        .dvi_hsync(dvi_hsync), .dvi_vsync(dvi_vsync)
    );

    multitini_audio_test audio_test (
        .control_clk(control_clk), .control_resetn(control_aresetn),
        .audio_mclk(audio_i2s_mclk),
        .enable_async(|(shell_control &
                        multitini_regs_pkg::MT_CONTROL_AUDIO_TEST_ENABLE)),
        .mute_async(|(shell_control &
                      multitini_regs_pkg::MT_CONTROL_AUDIO_MUTE)),
        .i2s_lrck(audio_i2s_lrck), .i2s_sdata(audio_i2s_sdata),
        .i2s_bclk(audio_i2s_bick), .spdif_out(audio_spdif_out)
    );

    // Stand-alone safety state. These constants must be inspected again after
    // synthesis and measured before an image is called hardware-safe.
    assign a2ctrl_irq_n   = 1'b1;
    assign a2ctrl_nmi_n   = 1'b1;
    assign a2ctrl_rdy_n   = 1'b1;
    assign a2ctrl_dma_n   = 1'b1;
    assign a2ctrl_inh_n   = 1'b1;
    assign a2ctrl_reset_n = 1'b1;
    assign a2fpga_oe_n    = 1'b1;
    assign a2fpga_oe_n_aux = 1'b0;
    assign a2fpga_dir_a   = 1'b0;
    assign a2fpga_dir_d   = 1'b0;
    assign a2fpga_a       = 16'hzzzz;
    assign a2fpga_d       = 8'hzz;
    assign a2fpga_rdwr_n  = 1'bz;
    assign a2fpga_dma_n   = 1'bz;
    assign a2fpga_rdy_n   = 1'bz;
    assign a2fpga_inh_n   = 1'bz;
    assign a2fpga_nmi_n   = 1'bz;
    assign a2fpga_reset_n = 1'bz;
    assign a2fpga_irq_n   = 1'bz;

    assign dvi_scl = 1'bz;
    assign dvi_sda = 1'bz;

    assign psram_a_io = 4'hz;
    assign psram_b_io = 4'hz;
    assign psram_ce_n = 1'b1;
    assign psram_clk  = 1'b0;

    assign eth_d     = 8'hzz;
    assign eth_a     = 2'b00;
    assign eth_rd_n  = 1'b1;
    assign eth_wr_n  = 1'b1;
    assign eth_cs_n  = 1'b1;
    assign eth_rst_n = 1'b0;

    wire unused_inputs = ^{idelay_ref_clk, a2fpga_clk, a2fpga_7m, a2fpga_q3,
                           a2fpga_m2b0, a2fpga_m2sel, a2fpga_devsel_n,
                           dvi_hpd, i2c0_rtc_int_n, i2c0_rtc_clkout,
                           i2c0_temp_alert_n, eth_int_n, usb_sys_fault_n};

endmodule

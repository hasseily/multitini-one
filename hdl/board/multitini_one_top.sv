// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

module multitini_one_top #(
    parameter logic [31:0] SHELL_BUILD_ID = 32'h00000000,
    parameter bit C64_NTSC_MODE = 1'b0,
    // 0 = C64 package, 1 = GBC package, 2 = supervisor-only boot shell.
    parameter integer ACTIVE_MACHINE = 2,
    parameter bit ENABLE_GBC_PSRAM = 1'b0
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
    logic c64_clk;
    logic control_aresetn;
    logic idelay_aresetn;
    logic video_aresetn;
    logic c64_aresetn;
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
    logic [15:0] reg_wr_addr;
    logic [31:0] reg_wr_data;
    logic [3:0]  reg_wr_strb;
    logic        reg_rd_en;
    logic [15:0] reg_rd_addr;
    logic [31:0] reg_rd_data;
    logic [31:0] control_rd_data;
    logic        control_wr_en;
    logic        control_rd_en;
    logic        ui_vram_wr_en;
    logic        ui_submit;
    logic        ui_submit_ready;
    logic        ui_commit_visible;
    logic        ui_commit_bank;
    logic        ui_commit_dense;
    logic        ui_active_visible_control;
    logic        ui_active_bank_control;
    logic        ui_active_dense_control;
    logic        ui_menu_pause_requested;
    logic        ui_audio_mute;
    logic        ui_pixel_opaque;
    logic [15:0] ui_pixel_rgb565;
    logic        video_enable;
    logic [11:0] video_scan_x;
    logic [11:0] video_scan_y;
    logic        video_vblank_start;
    logic        machine_video_active;
    logic [15:0] machine_video_rgb565;

    logic        c64_core_reset_request;
    logic        core_menu_pause;
    logic        c64_cpu_turbo_enable;
    logic        c64_drive_parallel_enable;
    logic [31:0] c64_core_id;
    logic        c64_ready;
    logic        c64_ready_launch;
    logic        c64_ready_control;
    logic        c64_load_error;
    logic        c64_load_error_launch;
    logic        c64_load_error_control;
    logic [2:0]  c64_rom_loaded;

    logic        load_submit;
    logic        load_submit_ready;
    logic [7:0]  load_kind;
    logic [7:0]  load_data;
    logic [31:0] load_offset;
    logic [31:0] load_total;
    logic        load_first;
    logic        load_last;
    logic        load_epoch;
    logic        load_abort;
    logic        load_abort_epoch;
    logic        load_error_clear;
    logic        c64_load_abort;
    logic        c64_load_abort_epoch;
    logic        load_abort_submit_ready;
    logic        load_abort_pending;
    logic        load_abort_pending_epoch;
    logic [0:0]  load_abort_mailbox_source;
    logic [0:0]  load_abort_mailbox_destination;
    logic        c64_load_error_clear;
    logic [82:0] load_mailbox_source;
    logic [82:0] load_mailbox_destination;
    logic        c64_load_valid;
    logic        c64_load_ready;
    logic [7:0]  c64_load_kind;
    logic [7:0]  c64_load_data;
    logic [31:0] c64_load_offset;
    logic [31:0] c64_load_total;
    logic        c64_load_first;
    logic        c64_load_last;
    logic        c64_load_epoch;

    logic        input_submit;
    logic        input_submit_ready;
    logic [15:0] input_code;
    logic [15:0] input_value;
    logic [31:0] input_mailbox_source;
    logic [31:0] input_mailbox_destination;
    logic        c64_input_valid;
    logic        c64_input_ready;
    logic        c64_input_mailbox_ready;
    logic [15:0] c64_input_code;
    logic [15:0] c64_input_value;

    logic        c64_media_request_valid;
    logic        c64_media_request_write;
    logic [31:0] c64_media_request_id;
    logic [31:0] c64_media_request_lba;
    logic [15:0] c64_media_request_block_count;
    logic [31:0] c64_media_request_generation;
    logic [7:0]  c64_media_write_data;
    logic        c64_media_write_data_valid;
    logic        c64_media_write_data_last;
    logic        c64_media_write_data_ready;
    logic        c64_media_state_mounted;
    logic        c64_media_state_dirty;
    logic        c64_media_state_eject_pending;
    logic        media_request_valid_control;
    logic        media_request_write_control;
    logic [31:0] media_request_id_control;
    logic [31:0] media_request_lba_control;
    logic [15:0] media_request_block_count_control;
    logic [31:0] media_request_generation_control;
    logic        media_mount;
    logic        media_eject;
    logic        media_complete;
    logic [7:0]  media_slot;
    logic [7:0]  media_kind;
    logic        media_read_only;
    logic [31:0] media_image_size;
    logic [31:0] media_generation;
    logic [31:0] media_complete_request_id;
    logic [31:0] media_complete_status;
    logic [15:0] media_complete_block_count;
    logic [31:0] media_complete_generation;
    logic        c64_media_mount;
    logic        c64_media_eject;
    logic        c64_media_complete;
    logic [7:0]  c64_media_slot;
    logic [7:0]  c64_media_kind;
    logic        c64_media_read_only;
    logic [31:0] c64_media_image_size;
    logic [31:0] c64_media_generation;
    logic [31:0] c64_media_complete_request_id;
    logic [31:0] c64_media_complete_status;
    logic [15:0] c64_media_complete_block_count;
    logic [31:0] c64_media_complete_generation;
    logic        media_submit_ready;
    logic [1:0]  media_command_operation;
    logic [194:0] media_command_mailbox_source;
    logic [194:0] media_command_mailbox_destination;
    logic        media_command_submit;
    logic        c64_media_command_valid;
    logic [1:0]  c64_media_command_operation;
    logic [112:0] media_request_mailbox_source;
    logic [112:0] media_request_mailbox_destination;
    logic        media_request_mailbox_src_valid;
    logic        media_request_mailbox_src_ready;
    logic        media_request_mailbox_dst_valid;
    logic        media_request_mailbox_dst_ready;
    logic        c64_media_request_sent;
    logic [8:0]  media_data_mailbox_destination;
    logic        media_data_valid_control;
    logic [7:0]  media_data_control;
    logic        media_data_last_control;
    logic        media_data_pop;
    logic [2:0]  media_state_mailbox_source;
    logic [2:0]  media_state_mailbox_destination;
    logic [2:0]  c64_media_state_sent;
    logic        media_state_mailbox_src_valid;
    logic        media_state_mailbox_src_ready;
    logic        media_state_mailbox_dst_valid;
    logic        media_state_mounted_control;
    logic        media_state_dirty_control;
    logic        media_state_eject_pending_control;

    (* KEEP = "yes" *) logic [7:0] c64_video_r;
    (* KEEP = "yes" *) logic [7:0] c64_video_g;
    (* KEEP = "yes" *) logic [7:0] c64_video_b;
    (* KEEP = "yes" *) logic       c64_video_de;
    (* KEEP = "yes" *) logic       c64_video_hsync;
    (* KEEP = "yes" *) logic       c64_video_vsync;
    (* KEEP = "yes" *) logic       c64_video_valid;
    (* KEEP = "yes" *) logic signed [23:0] c64_audio_l;
    (* KEEP = "yes" *) logic signed [23:0] c64_audio_r;
    (* KEEP = "yes" *) logic               c64_audio_valid;

    logic [31:0] gbc_cart_size;
    logic        gbc_cart_loaded;
    logic [23:0] c64_memory_addr;
    logic        c64_memory_write;
    logic [63:0] c64_memory_wdata;
    logic [7:0]  c64_memory_wstrb;
    logic        c64_memory_valid;
    logic        c64_memory_ready;
    logic [63:0] c64_memory_rdata;
    logic        c64_memory_rvalid;
    logic        c64_memory_fault;
    logic        c64_prg_loaded;
    logic        c64_cart_loaded;
    logic [7:0]  c64_cart_hardware_type;
    logic        c64_prg_run_busy;
    logic [23:0] gbc_memory_addr;
    logic        gbc_memory_write;
    logic [63:0] gbc_memory_wdata;
    logic [7:0]  gbc_memory_wstrb;
    logic        gbc_memory_valid;
    logic        gbc_memory_ready;
    logic [63:0] gbc_memory_rdata;
    logic        gbc_memory_rvalid;
    logic        gbc_memory_fault;

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
        .video_clk(video_clk), .c64_clk(c64_clk),
        .control_aresetn(control_aresetn),
        .idelay_aresetn(idelay_aresetn),
        .video_aresetn(video_aresetn), .c64_aresetn(c64_aresetn),
        .IRQ(shell_irq)
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
        .core_id(c64_core_id),
        .core_variant_index(ACTIVE_MACHINE == 0 && C64_NTSC_MODE ?
                            32'd1 : 32'd0),
        .core_ready(c64_ready_control),
        .load_submit_ready(load_submit_ready),
        .core_load_error(c64_load_error_control),
        .input_submit_ready(input_submit_ready),
        .media_request_valid(media_request_valid_control),
        .media_request_write(media_request_write_control),
        .media_request_id(media_request_id_control),
        .media_request_lba(media_request_lba_control),
        .media_request_block_count(media_request_block_count_control),
        .media_request_generation(media_request_generation_control),
        .media_data_valid(media_data_valid_control),
        .media_data(media_data_control),
        .media_data_last(media_data_last_control),
        .media_state_mounted(media_state_mounted_control),
        .media_state_dirty(media_state_dirty_control),
        .media_state_eject_pending(media_state_eject_pending_control),
        .media_submit_ready(media_submit_ready),
        .ui_active_visible(ui_active_visible_control),
        .ui_active_bank(ui_active_bank_control),
        .ui_active_dense(ui_active_dense_control),
        .ui_submit_ready(ui_submit_ready),
        .wr_en(control_wr_en), .wr_addr(reg_wr_addr[11:0]),
        .wr_data(reg_wr_data),
        .wr_strb(reg_wr_strb), .rd_en(control_rd_en),
        .rd_addr(reg_rd_addr[11:0]),
        .rd_data(control_rd_data), .control(shell_control),
        .load_submit(load_submit), .load_kind(load_kind),
        .load_data(load_data), .load_offset(load_offset),
        .load_total(load_total), .load_first(load_first),
        .load_last(load_last), .load_epoch(load_epoch),
        .load_abort(load_abort), .load_abort_epoch(load_abort_epoch),
        .load_error_clear(load_error_clear),
        .input_submit(input_submit), .input_code(input_code),
        .input_value(input_value),
        .media_mount(media_mount), .media_eject(media_eject),
        .media_complete(media_complete), .media_slot(media_slot),
        .media_kind(media_kind), .media_read_only(media_read_only),
        .media_image_size(media_image_size),
        .media_generation(media_generation),
        .media_complete_request_id(media_complete_request_id),
        .media_complete_status(media_complete_status),
        .media_complete_block_count(media_complete_block_count),
        .media_complete_generation(media_complete_generation),
        .media_data_pop(media_data_pop),
        .ui_submit(ui_submit), .ui_visible(ui_commit_visible),
        .ui_bank(ui_commit_bank),
        .ui_dense(ui_commit_dense),
        .irq(shell_irq)
    );

    always_comb begin
        control_wr_en = reg_wr_en && reg_wr_addr < 16'h1000;
        control_rd_en = reg_rd_en && reg_rd_addr < 16'h1000;
        ui_vram_wr_en = reg_wr_en &&
            ((reg_wr_addr >= MT_UI_VRAM_BANK0_OFFSET &&
              reg_wr_addr < MT_UI_VRAM_BANK0_OFFSET + MT_UI_VRAM_BANK_BYTES) ||
             (reg_wr_addr >= MT_UI_VRAM_BANK1_OFFSET &&
              reg_wr_addr < MT_UI_VRAM_BANK1_OFFSET + MT_UI_VRAM_BANK_BYTES) ||
             (reg_wr_addr >= MT_UI_LEGACY_VRAM_BANK0_OFFSET &&
              reg_wr_addr < MT_UI_LEGACY_VRAM_BANK0_OFFSET +
                            MT_UI_LEGACY_VRAM_BANK_BYTES) ||
             (reg_wr_addr >= MT_UI_LEGACY_VRAM_BANK1_OFFSET &&
              reg_wr_addr < MT_UI_LEGACY_VRAM_BANK1_OFFSET +
                            MT_UI_LEGACY_VRAM_BANK_BYTES));
        reg_rd_data = reg_rd_addr < 16'h1000 ? control_rd_data : 32'd0;
    end

    assign load_mailbox_source = {load_kind, load_data, load_offset,
                                  load_total, load_first, load_last,
                                  load_epoch};
    assign {c64_load_kind, c64_load_data, c64_load_offset, c64_load_total,
            c64_load_first, c64_load_last, c64_load_epoch} =
        load_mailbox_destination;

    multitini_async_mailbox #(.WIDTH(83)) c64_load_mailbox (
        .src_clk(control_clk), .src_resetn(control_aresetn),
        .src_data(load_mailbox_source), .src_valid(load_submit),
        .src_ready(load_submit_ready),
        .dst_clk(c64_clk), .dst_resetn(c64_aresetn),
        .dst_data(load_mailbox_destination), .dst_valid(c64_load_valid),
        .dst_ready(c64_load_ready)
    );

    assign input_mailbox_source = {input_code, input_value};
    assign {c64_input_code, c64_input_value} = input_mailbox_destination;
    // Consume and discard any already queued event while the machine reset is
    // asserted so a stale key press cannot appear after reset release.
    assign c64_input_mailbox_ready = c64_input_ready ||
                                     c64_core_reset_request;

    multitini_async_mailbox #(.WIDTH(32)) c64_input_mailbox (
        .src_clk(control_clk), .src_resetn(control_aresetn),
        .src_data(input_mailbox_source), .src_valid(input_submit),
        .src_ready(input_submit_ready),
        .dst_clk(c64_clk), .dst_resetn(c64_aresetn),
        .dst_data(input_mailbox_destination), .dst_valid(c64_input_valid),
        .dst_ready(c64_input_mailbox_ready)
    );

    cdc_bit_sync c64_reset_sync (
        .clk(c64_clk), .resetn(c64_aresetn),
        .din(|(shell_control &
               multitini_regs_pkg::MT_CONTROL_CORE_RESET_REQUEST)),
        .dout(c64_core_reset_request)
    );

    cdc_bit_sync c64_cpu_turbo_sync (
        .clk(c64_clk), .resetn(c64_aresetn),
        .din(|(shell_control &
               multitini_regs_pkg::MT_CONTROL_C64_CPU_TURBO_ENABLE)),
        .dout(c64_cpu_turbo_enable)
    );

    cdc_bit_sync c64_drive_parallel_sync (
        .clk(c64_clk), .resetn(c64_aresetn),
        .din(|(shell_control &
               multitini_regs_pkg::MT_CONTROL_C64_DRIVE_PARALLEL_ENABLE)),
        .dout(c64_drive_parallel_enable)
    );

    // Launch status from a machine-clocked register.  Feeding the combinational
    // adapter status directly into a synchronizer creates a CDC-10 hazard.
    always_ff @(posedge c64_clk) begin
        if (!c64_aresetn) begin
            c64_ready_launch <= 1'b0;
            c64_load_error_launch <= 1'b0;
        end else begin
            c64_ready_launch <= c64_ready;
            c64_load_error_launch <= c64_load_error;
        end
    end

    cdc_bit_sync c64_ready_sync (
        .clk(control_clk), .resetn(control_aresetn),
        .din(c64_ready_launch), .dout(c64_ready_control)
    );

    cdc_bit_sync c64_load_error_sync (
        .clk(control_clk), .resetn(control_aresetn),
        .din(c64_load_error_launch), .dout(c64_load_error_control)
    );

    // Carry the abort event and its stream epoch through one coherent
    // handshake.  The destination accepts it immediately, so dst_valid is a
    // single core-clock pulse and the epoch cannot lag or lead that pulse.
    always_ff @(posedge control_clk) begin
        if (!control_aresetn) begin
            load_abort_pending <= 1'b0;
            load_abort_pending_epoch <= 1'b0;
        end else if (load_abort) begin
            load_abort_pending <= 1'b1;
            load_abort_pending_epoch <= load_abort_epoch;
        end else if (load_abort_pending && load_abort_submit_ready) begin
            load_abort_pending <= 1'b0;
        end
    end

    assign load_abort_mailbox_source = load_abort_pending_epoch;
    assign c64_load_abort_epoch = load_abort_mailbox_destination[0];
    multitini_async_mailbox #(.WIDTH(1)) c64_load_abort_mailbox (
        .src_clk(control_clk), .src_resetn(control_aresetn),
        .src_data(load_abort_mailbox_source),
        .src_valid(load_abort_pending),
        .src_ready(load_abort_submit_ready),
        .dst_clk(c64_clk), .dst_resetn(c64_aresetn),
        .dst_data(load_abort_mailbox_destination),
        .dst_valid(c64_load_abort), .dst_ready(1'b1)
    );

    multitini_cdc_pulse_toggle c64_load_error_clear_sync (
        .src_clk(control_clk), .src_resetn(control_aresetn),
        .src_pulse(load_error_clear), .dst_clk(c64_clk),
        .dst_resetn(c64_aresetn), .dst_pulse(c64_load_error_clear)
    );

    // A request is captured once and held coherently in the control domain
    // until software completes or ejects it. The source latch prevents the
    // still-asserted core request from being submitted twice.
    assign media_request_mailbox_source = {
        c64_media_request_write, c64_media_request_id,
        c64_media_request_lba, c64_media_request_block_count,
        c64_media_request_generation
    };
    assign media_request_mailbox_src_valid =
        c64_media_request_valid && !c64_media_request_sent;
    // While a soft core reset is asserted, acknowledge and discard any
    // pre-reset mailbox item so it cannot reappear as a stale MMIO request.
    assign media_request_mailbox_dst_ready =
        !media_request_valid_control ||
        |(shell_control & MT_CONTROL_CORE_RESET_REQUEST);

    always_ff @(posedge c64_clk) begin
        if (!c64_aresetn || c64_core_reset_request)
            c64_media_request_sent <= 1'b0;
        else if (!c64_media_request_valid)
            c64_media_request_sent <= 1'b0;
        else if (media_request_mailbox_src_valid &&
                 media_request_mailbox_src_ready)
            c64_media_request_sent <= 1'b1;
    end

    multitini_async_mailbox #(.WIDTH(113)) media_request_mailbox (
        .src_clk(c64_clk), .src_resetn(c64_aresetn),
        .src_data(media_request_mailbox_source),
        .src_valid(media_request_mailbox_src_valid),
        .src_ready(media_request_mailbox_src_ready),
        .dst_clk(control_clk), .dst_resetn(control_aresetn),
        .dst_data(media_request_mailbox_destination),
        .dst_valid(media_request_mailbox_dst_valid),
        .dst_ready(media_request_mailbox_dst_ready)
    );

    // Writeback bytes remain stable in the drive domain until software reads
    // MEDIA_DATA. The acknowledgement then backpressures the next byte until
    // this one-entry CDC mailbox has completed its round trip.
    assign {media_data_last_control, media_data_control} =
        media_data_mailbox_destination;
    multitini_async_mailbox #(.WIDTH(9)) media_data_mailbox (
        .src_clk(c64_clk), .src_resetn(c64_aresetn),
        .src_data({c64_media_write_data_last, c64_media_write_data}),
        .src_valid(c64_media_write_data_valid),
        .src_ready(c64_media_write_data_ready),
        .dst_clk(control_clk), .dst_resetn(control_aresetn),
        .dst_data(media_data_mailbox_destination),
        .dst_valid(media_data_valid_control), .dst_ready(media_data_pop)
    );

    // Status is sparse but the three bits form one state machine. Carry each
    // changed snapshot coherently so software never observes a mixed-domain
    // mounted/dirty/eject-pending combination.
    assign media_state_mailbox_source = {
        c64_media_state_eject_pending, c64_media_state_dirty,
        c64_media_state_mounted
    };
    assign media_state_mailbox_src_valid =
        media_state_mailbox_source != c64_media_state_sent;
    always_ff @(posedge c64_clk) begin
        if (!c64_aresetn)
            c64_media_state_sent <= 3'd0;
        else if (media_state_mailbox_src_valid &&
                 media_state_mailbox_src_ready)
            c64_media_state_sent <= media_state_mailbox_source;
    end
    multitini_async_mailbox #(.WIDTH(3)) media_state_mailbox (
        .src_clk(c64_clk), .src_resetn(c64_aresetn),
        .src_data(media_state_mailbox_source),
        .src_valid(media_state_mailbox_src_valid),
        .src_ready(media_state_mailbox_src_ready),
        .dst_clk(control_clk), .dst_resetn(control_aresetn),
        .dst_data(media_state_mailbox_destination),
        .dst_valid(media_state_mailbox_dst_valid), .dst_ready(1'b1)
    );
    always_ff @(posedge control_clk) begin
        if (!control_aresetn) begin
            media_state_mounted_control <= 1'b0;
            media_state_dirty_control <= 1'b0;
            media_state_eject_pending_control <= 1'b0;
        end else if (media_state_mailbox_dst_valid) begin
            {media_state_eject_pending_control, media_state_dirty_control,
             media_state_mounted_control} <=
                media_state_mailbox_destination;
        end
    end

    always_ff @(posedge control_clk) begin
        if (!control_aresetn) begin
            media_request_valid_control <= 1'b0;
            media_request_write_control <= 1'b0;
            media_request_id_control <= 32'd0;
            media_request_lba_control <= 32'd0;
            media_request_block_count_control <= 16'd0;
            media_request_generation_control <= 32'd0;
        end else if (|(shell_control & MT_CONTROL_CORE_RESET_REQUEST)) begin
            media_request_valid_control <= 1'b0;
            media_request_write_control <= 1'b0;
            media_request_id_control <= 32'd0;
            media_request_lba_control <= 32'd0;
            media_request_block_count_control <= 16'd0;
            media_request_generation_control <= 32'd0;
        end else begin
            // A dirty eject deliberately preserves an outstanding reverse
            // write request in the drive domain. Keep the matching coherent
            // MMIO snapshot until software completes that write; clearing it
            // here would also leave the source request latch asserted, so the
            // request could never be submitted again.
            if (media_mount || media_complete ||
                (media_eject && !media_request_write_control))
                media_request_valid_control <= 1'b0;
            if (media_request_mailbox_dst_valid &&
                media_request_mailbox_dst_ready) begin
                {media_request_write_control, media_request_id_control,
                 media_request_lba_control,
                 media_request_block_count_control,
                 media_request_generation_control} <=
                    media_request_mailbox_destination;
                media_request_valid_control <= 1'b1;
            end
        end
    end

    // Shell media commands and every associated field cross as one atomic
    // mailbox payload. Backpressure reaches the command decoder.
    always_comb begin
        if (media_mount)
            media_command_operation = 2'd1;
        else if (media_eject)
            media_command_operation = 2'd2;
        else if (media_complete)
            media_command_operation = 2'd3;
        else
            media_command_operation = 2'd0;
    end
    assign media_command_submit = media_command_operation != 2'd0;
    assign media_command_mailbox_source = {
        media_command_operation, media_slot, media_kind, media_read_only,
        media_image_size, media_generation, media_complete_request_id,
        media_complete_status, media_complete_block_count,
        media_complete_generation
    };
    assign {c64_media_command_operation, c64_media_slot, c64_media_kind,
            c64_media_read_only, c64_media_image_size,
            c64_media_generation, c64_media_complete_request_id,
            c64_media_complete_status, c64_media_complete_block_count,
            c64_media_complete_generation} =
        media_command_mailbox_destination;
    assign c64_media_mount = c64_media_command_valid &&
                             c64_media_command_operation == 2'd1;
    assign c64_media_eject = c64_media_command_valid &&
                             c64_media_command_operation == 2'd2;
    assign c64_media_complete = c64_media_command_valid &&
                                c64_media_command_operation == 2'd3;

    multitini_async_mailbox #(.WIDTH(195)) media_command_mailbox (
        .src_clk(control_clk), .src_resetn(control_aresetn),
        .src_data(media_command_mailbox_source),
        .src_valid(media_command_submit), .src_ready(media_submit_ready),
        .dst_clk(c64_clk), .dst_resetn(c64_aresetn),
        .dst_data(media_command_mailbox_destination),
        .dst_valid(c64_media_command_valid), .dst_ready(1'b1)
    );

    // Dense-menu visibility is a shared pause request, independent of reset.
    // Its synchronized state comes from the vblank-committed overlay, so a
    // stalled ARM service cannot leave a visible menu over a running game.
    // Legacy firmware does not hide its overlay before synchronous LOADs;
    // retaining its old behavior keeps ABI 0.11 backward compatible.
    // Register combined controls before either clock-domain crossing. A
    // combinational expression at a synchronizer input can carry glitches.
    always_ff @(posedge control_clk) begin
        if (!control_aresetn) begin
            ui_menu_pause_requested <= 1'b0;
            ui_audio_mute <= 1'b1;
        end else begin
            ui_menu_pause_requested <= ui_active_visible_control &&
                                       ui_active_dense_control;
            ui_audio_mute <= (ui_active_visible_control &&
                             ui_active_dense_control) ||
                            |(shell_control & MT_CONTROL_AUDIO_MUTE);
        end
    end
    cdc_bit_sync core_menu_pause_sync (
        .clk(c64_clk), .resetn(c64_aresetn),
        .din(ui_menu_pause_requested), .dout(core_menu_pause)
    );

    // Build one complete machine at a time. Both C64 standards use the same
    // whole-frame ring. The 6569 PAL blanking window is 382x270; the selected
    // 6567R8 NTSC timing is 404x250. Four-times scaling fills PAL vertically
    // and centers the shorter NTSC raster in the 1920x1080 active area.
    generate
        if (ACTIVE_MACHINE == 0) begin : g_c64_machine
            assign gbc_cart_size = 32'd0;
            assign gbc_cart_loaded = 1'b0;
            assign gbc_memory_fault = 1'b0;
            (* DONT_TOUCH = "yes" *) multitini_c64_adapter #(
                .NTSC_MODE(C64_NTSC_MODE)
            ) machine_adapter (
                .core_clk(c64_clk), .reset(!c64_aresetn),
                .core_reset_request(c64_core_reset_request),
                .pause_request(core_menu_pause),
                .cpu_turbo_enable(c64_cpu_turbo_enable),
                .drive_parallel_enable(c64_drive_parallel_enable),
                .load_abort(c64_load_abort),
                .load_abort_epoch(c64_load_abort_epoch),
                .clear_load_error(c64_load_error_clear),
                .input_code(c64_input_code), .input_value(c64_input_value),
                .input_valid(c64_input_valid), .input_ready(c64_input_ready),
                .load_data(c64_load_data), .load_valid(c64_load_valid),
                .load_ready(c64_load_ready), .load_first(c64_load_first),
                .load_last(c64_load_last), .load_kind(c64_load_kind),
                .load_offset(c64_load_offset), .load_total(c64_load_total),
                .load_epoch(c64_load_epoch),
                .memory_addr(c64_memory_addr),
                .memory_write(c64_memory_write),
                .memory_wdata(c64_memory_wdata),
                .memory_wstrb(c64_memory_wstrb),
                .memory_valid(c64_memory_valid),
                .memory_ready(c64_memory_ready),
                .memory_rdata(c64_memory_rdata),
                .memory_rvalid(c64_memory_rvalid),
                .memory_fault(c64_memory_fault),
                .media_mount(c64_media_mount),
                .media_eject(c64_media_eject),
                .media_complete(c64_media_complete),
                .media_slot(c64_media_slot), .media_kind(c64_media_kind),
                .media_read_only(c64_media_read_only),
                .media_image_size(c64_media_image_size),
                .media_generation(c64_media_generation),
                .media_complete_request_id(c64_media_complete_request_id),
                .media_complete_status(c64_media_complete_status),
                .media_complete_block_count(c64_media_complete_block_count),
                .media_complete_generation(c64_media_complete_generation),
                .media_request_valid(c64_media_request_valid),
                .media_request_write(c64_media_request_write),
                .media_request_id(c64_media_request_id),
                .media_request_lba(c64_media_request_lba),
                .media_request_block_count(c64_media_request_block_count),
                .media_request_generation(c64_media_request_generation),
                .media_write_data(c64_media_write_data),
                .media_write_data_valid(c64_media_write_data_valid),
                .media_write_data_last(c64_media_write_data_last),
                .media_write_data_ready(c64_media_write_data_ready),
                .media_state_mounted(c64_media_state_mounted),
                .media_state_dirty(c64_media_state_dirty),
                .media_state_eject_pending(c64_media_state_eject_pending),
                .core_id(c64_core_id), .ready(c64_ready),
                .load_error(c64_load_error), .rom_loaded(c64_rom_loaded),
                .prg_loaded(c64_prg_loaded),
                .cart_loaded(c64_cart_loaded),
                .cart_hardware_type(c64_cart_hardware_type),
                .prg_run_busy(c64_prg_run_busy),
                .video_r(c64_video_r), .video_g(c64_video_g),
                .video_b(c64_video_b), .video_de(c64_video_de),
                .video_hsync(c64_video_hsync),
                .video_vsync(c64_video_vsync),
                .video_valid(c64_video_valid), .audio_l(c64_audio_l),
                .audio_r(c64_audio_r), .audio_valid(c64_audio_valid)
            );

            multitini_c64_video_bridge #(
                .NTSC_MODE(C64_NTSC_MODE)
            ) video_bridge (
                .source_clk(c64_clk), .source_resetn(c64_aresetn),
                .source_ready(c64_ready),
                .source_r(c64_video_r), .source_g(c64_video_g),
                .source_b(c64_video_b), .source_de(c64_video_de),
                .source_vsync(c64_video_vsync),
                .source_valid(c64_video_valid),
                .pixel_clk(video_clk), .pixel_resetn(video_aresetn),
                .diagnostic_request(video_enable),
                .scan_x(video_scan_x), .scan_y(video_scan_y),
                .vblank_start(video_vblank_start),
                .machine_active(machine_video_active),
                .machine_rgb565(machine_video_rgb565)
            );
            multitini_gameboy_psram_path psram_path_i (
                .core_clk(c64_clk), .core_resetn(c64_aresetn),
                .control_clk(control_clk),
                .control_resetn(control_aresetn),
                .idelay_ref_clk(idelay_ref_clk),
                .idelay_resetn(idelay_aresetn),
                .core_memory_addr(c64_memory_addr),
                .core_memory_write(c64_memory_write),
                .core_memory_wdata(c64_memory_wdata),
                .core_memory_wstrb(c64_memory_wstrb),
                .core_memory_valid(c64_memory_valid),
                .core_memory_ready(c64_memory_ready),
                .core_memory_rdata(c64_memory_rdata),
                .core_memory_rvalid(c64_memory_rvalid),
                .memory_fault(c64_memory_fault),
                .psram_a_io(psram_a_io), .psram_b_io(psram_b_io),
                .psram_ce_n(psram_ce_n), .psram_clk(psram_clk)
            );
        end else if (ACTIVE_MACHINE == 1) begin : g_gameboy_machine
            assign c64_rom_loaded = {2'd0, gbc_cart_loaded};
            (* DONT_TOUCH = "yes" *) multitini_gameboy_adapter machine_adapter (
                .core_clk(c64_clk), .reset(!c64_aresetn),
                .core_reset_request(c64_core_reset_request),
                .pause_request(core_menu_pause),
                .clear_load_error(c64_load_error_clear),
                .input_code(c64_input_code), .input_value(c64_input_value),
                .input_valid(c64_input_valid), .input_ready(c64_input_ready),
                .load_data(c64_load_data), .load_valid(c64_load_valid),
                .load_ready(c64_load_ready), .load_first(c64_load_first),
                .load_last(c64_load_last), .load_kind(c64_load_kind),
                .load_offset(c64_load_offset), .load_total(c64_load_total),
                .memory_addr(gbc_memory_addr),
                .memory_write(gbc_memory_write),
                .memory_wdata(gbc_memory_wdata),
                .memory_wstrb(gbc_memory_wstrb),
                .memory_valid(gbc_memory_valid),
                .memory_ready(gbc_memory_ready),
                .memory_rdata(gbc_memory_rdata),
                .memory_rvalid(gbc_memory_rvalid),
                .memory_fault(gbc_memory_fault),
                .media_mount(c64_media_mount),
                .media_eject(c64_media_eject),
                .media_complete(c64_media_complete),
                .media_slot(c64_media_slot), .media_kind(c64_media_kind),
                .media_read_only(c64_media_read_only),
                .media_image_size(c64_media_image_size),
                .media_generation(c64_media_generation),
                .media_complete_request_id(c64_media_complete_request_id),
                .media_complete_status(c64_media_complete_status),
                .media_complete_block_count(c64_media_complete_block_count),
                .media_complete_generation(c64_media_complete_generation),
                .media_request_valid(c64_media_request_valid),
                .media_request_write(c64_media_request_write),
                .media_request_id(c64_media_request_id),
                .media_request_lba(c64_media_request_lba),
                .media_request_block_count(c64_media_request_block_count),
                .media_request_generation(c64_media_request_generation),
                .media_write_data(c64_media_write_data),
                .media_write_data_valid(c64_media_write_data_valid),
                .media_write_data_last(c64_media_write_data_last),
                .media_write_data_ready(c64_media_write_data_ready),
                .media_state_mounted(c64_media_state_mounted),
                .media_state_dirty(c64_media_state_dirty),
                .media_state_eject_pending(c64_media_state_eject_pending),
                .core_id(c64_core_id), .ready(c64_ready),
                .load_error(c64_load_error), .cart_loaded(gbc_cart_loaded),
                .cart_size(gbc_cart_size),
                .video_r(c64_video_r), .video_g(c64_video_g),
                .video_b(c64_video_b), .video_de(c64_video_de),
                .video_hsync(c64_video_hsync),
                .video_vsync(c64_video_vsync),
                .video_valid(c64_video_valid), .audio_l(c64_audio_l),
                .audio_r(c64_audio_r), .audio_valid(c64_audio_valid)
            );

            multitini_gameboy_video_bridge video_bridge (
                .source_clk(c64_clk), .source_resetn(c64_aresetn),
                .source_ready(c64_ready),
                .source_r(c64_video_r), .source_g(c64_video_g),
                .source_b(c64_video_b), .source_de(c64_video_de),
                .source_vsync(c64_video_vsync),
                .source_valid(c64_video_valid),
                .pixel_clk(video_clk), .pixel_resetn(video_aresetn),
                .diagnostic_request(video_enable),
                .scan_x(video_scan_x), .scan_y(video_scan_y),
                .vblank_start(video_vblank_start),
                .machine_active(machine_video_active),
                .machine_rgb565(machine_video_rgb565)
            );

            if (ENABLE_GBC_PSRAM) begin : g_psram_active
                multitini_gameboy_psram_path psram_path_i (
                    .core_clk(c64_clk), .core_resetn(c64_aresetn),
                    .control_clk(control_clk),
                    .control_resetn(control_aresetn),
                    .idelay_ref_clk(idelay_ref_clk),
                    .idelay_resetn(idelay_aresetn),
                    .core_memory_addr(gbc_memory_addr),
                    .core_memory_write(gbc_memory_write),
                    .core_memory_wdata(gbc_memory_wdata),
                    .core_memory_wstrb(gbc_memory_wstrb),
                    .core_memory_valid(gbc_memory_valid),
                    .core_memory_ready(gbc_memory_ready),
                    .core_memory_rdata(gbc_memory_rdata),
                    .core_memory_rvalid(gbc_memory_rvalid),
                    .memory_fault(gbc_memory_fault),
                    .psram_a_io(psram_a_io), .psram_b_io(psram_b_io),
                    .psram_ce_n(psram_ce_n), .psram_clk(psram_clk)
                );
            end else begin : g_psram_inactive
                assign gbc_memory_ready = 1'b0;
                assign gbc_memory_rdata = 64'd0;
                assign gbc_memory_rvalid = 1'b0;
                assign gbc_memory_fault = 1'b0;
                assign psram_a_io = 4'hz;
                assign psram_b_io = 4'hz;
                assign psram_ce_n = 1'b1;
                assign psram_clk  = 1'b0;
            end
        end else begin : g_supervisor_shell
            // The boot shell owns the shared control/UI/video plumbing but
            // deliberately contains no machine. Every core is loaded from an
            // SD package by the supervisor firmware.
            assign c64_core_id = 32'd0;
            assign c64_ready = 1'b0;
            assign c64_load_error = 1'b0;
            assign c64_rom_loaded = 3'd0;
            assign c64_load_ready = 1'b0;
            assign c64_input_ready = 1'b0;

            assign c64_media_request_valid = 1'b0;
            assign c64_media_request_write = 1'b0;
            assign c64_media_request_id = 32'd0;
            assign c64_media_request_lba = 32'd0;
            assign c64_media_request_block_count = 16'd0;
            assign c64_media_request_generation = 32'd0;
            assign c64_media_write_data = 8'd0;
            assign c64_media_write_data_valid = 1'b0;
            assign c64_media_write_data_last = 1'b0;
            assign c64_media_state_mounted = 1'b0;
            assign c64_media_state_dirty = 1'b0;
            assign c64_media_state_eject_pending = 1'b0;

            assign c64_prg_loaded = 1'b0;
            assign c64_cart_loaded = 1'b0;
            assign c64_cart_hardware_type = 8'd0;
            assign c64_prg_run_busy = 1'b0;
            assign c64_video_r = 8'd0;
            assign c64_video_g = 8'd0;
            assign c64_video_b = 8'd0;
            assign c64_video_de = 1'b0;
            assign c64_video_hsync = 1'b0;
            assign c64_video_vsync = 1'b0;
            assign c64_video_valid = 1'b0;
            assign c64_audio_l = 24'sd0;
            assign c64_audio_r = 24'sd0;
            assign c64_audio_valid = 1'b0;
            assign machine_video_active = 1'b0;
            assign machine_video_rgb565 = 16'd0;

            assign c64_memory_addr = 24'd0;
            assign c64_memory_write = 1'b0;
            assign c64_memory_wdata = 64'd0;
            assign c64_memory_wstrb = 8'd0;
            assign c64_memory_valid = 1'b0;
            assign c64_memory_ready = 1'b0;
            assign c64_memory_rdata = 64'd0;
            assign c64_memory_rvalid = 1'b0;
            assign c64_memory_fault = 1'b0;
            assign gbc_cart_size = 32'd0;
            assign gbc_cart_loaded = 1'b0;
            assign gbc_memory_addr = 24'd0;
            assign gbc_memory_write = 1'b0;
            assign gbc_memory_wdata = 64'd0;
            assign gbc_memory_wstrb = 8'd0;
            assign gbc_memory_valid = 1'b0;
            assign gbc_memory_ready = 1'b0;
            assign gbc_memory_rdata = 64'd0;
            assign gbc_memory_rvalid = 1'b0;
            assign gbc_memory_fault = 1'b0;

            assign psram_a_io = 4'hz;
            assign psram_b_io = 4'hz;
            assign psram_ce_n = 1'b1;
            assign psram_clk = 1'b0;
        end
    endgenerate

    cdc_bit_sync video_enable_sync (
        .clk(video_clk), .resetn(video_aresetn),
        .din(|(shell_control &
               multitini_regs_pkg::MT_CONTROL_VIDEO_TEST_ENABLE)),
        .dout(video_enable)
    );

    multitini_text_overlay text_overlay (
        .control_clk(control_clk), .control_resetn(control_aresetn),
        .vram_wr_en(ui_vram_wr_en), .vram_wr_addr(reg_wr_addr),
        .vram_wr_data(reg_wr_data), .vram_wr_strb(reg_wr_strb),
        .commit_valid(ui_submit), .commit_visible(ui_commit_visible),
        .commit_bank(ui_commit_bank), .commit_ready(ui_submit_ready),
        .commit_dense(ui_commit_dense),
        .active_visible_control(ui_active_visible_control),
        .active_bank_control(ui_active_bank_control),
        .active_dense_control(ui_active_dense_control),
        .pixel_clk(video_clk), .pixel_resetn(video_aresetn),
        .scan_x(video_scan_x), .scan_y(video_scan_y),
        .vblank_start(video_vblank_start),
        .pixel_opaque(ui_pixel_opaque), .pixel_rgb565(ui_pixel_rgb565)
    );

    multitini_video_test video_test (
        .pixel_clk(video_clk), .resetn(video_aresetn), .enable(video_enable),
        .machine_enable(machine_video_active),
        .machine_rgb565(machine_video_rgb565),
        .ui_opaque(ui_pixel_opaque), .ui_rgb565(ui_pixel_rgb565),
        .scan_x(video_scan_x), .scan_y(video_scan_y),
        .vblank_start(video_vblank_start),
        .dvi_red(dvi_red), .dvi_grn(dvi_grn), .dvi_blu(dvi_blu),
        .dvi_clk(dvi_clk), .dvi_de(dvi_de),
        .dvi_hsync(dvi_hsync), .dvi_vsync(dvi_vsync)
    );

    multitini_audio_test audio_test (
        .machine_clk(c64_clk), .machine_resetn(c64_aresetn),
        .machine_ready(c64_ready),
        .machine_audio_l(c64_audio_l), .machine_audio_r(c64_audio_r),
        .machine_audio_valid(c64_audio_valid),
        .control_clk(control_clk), .control_resetn(control_aresetn),
        .audio_mclk(audio_i2s_mclk),
        .test_tone_async(|(shell_control &
                           multitini_regs_pkg::MT_CONTROL_AUDIO_TEST_ENABLE)),
        .mute_async(ui_audio_mute),
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

    assign eth_d     = 8'hzz;
    assign eth_a     = 2'b00;
    assign eth_rd_n  = 1'b1;
    assign eth_wr_n  = 1'b1;
    assign eth_cs_n  = 1'b1;
    assign eth_rst_n = 1'b0;

    wire unused_inputs = ^{idelay_ref_clk, idelay_aresetn,
                           a2fpga_clk, a2fpga_7m, a2fpga_q3,
                           a2fpga_m2b0, a2fpga_m2sel, a2fpga_devsel_n,
                           dvi_hpd, i2c0_rtc_int_n, i2c0_rtc_clkout,
                           i2c0_temp_alert_n, eth_int_n, usb_sys_fault_n,
                           gbc_cart_size, gbc_memory_fault};

endmodule

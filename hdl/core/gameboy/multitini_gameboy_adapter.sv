// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// Game Boy Color adapter around the pinned MiSTer machine logic. Physical
// video scaling, audio transport, USB translation, save files, and wall-clock
// RTC persistence remain shell/firmware services.
module multitini_gameboy_adapter #(
    parameter integer AUDIO_SAMPLE_HZ = 48000
) (
    input  logic         core_clk,
    input  logic         reset,
    input  logic         core_reset_request,
    input  logic         clear_load_error,

    input  logic [15:0]  input_code,
    input  logic [15:0]  input_value,
    input  logic         input_valid,
    output logic         input_ready,

    input  logic [7:0]   load_data,
    input  logic         load_valid,
    output logic         load_ready,
    input  logic         load_first,
    input  logic         load_last,
    input  logic [7:0]   load_kind,
    input  logic [31:0]  load_offset,
    input  logic [31:0]  load_total,

    output logic [23:0]  memory_addr,
    output logic         memory_write,
    output logic [63:0]  memory_wdata,
    output logic [7:0]   memory_wstrb,
    output logic         memory_valid,
    input  logic         memory_ready,
    input  logic [63:0]  memory_rdata,
    input  logic         memory_rvalid,
    input  logic         memory_fault,

    output logic [31:0]  core_id,
    output logic         ready,
    output logic         load_error,
    output logic         cart_loaded,
    output logic [31:0]  cart_size,

    output logic [7:0]   video_r,
    output logic [7:0]   video_g,
    output logic [7:0]   video_b,
    output logic         video_de,
    output logic         video_hsync,
    output logic         video_vsync,
    output logic         video_valid,
    output logic signed [23:0] audio_l,
    output logic signed [23:0] audio_r,
    output logic                audio_valid
);
    import multitini_gameboy_defs_pkg::*;

    logic machine_reset;
    logic load_active;
    logic boot_override_loaded;
    logic ioctl_download;
    logic cart_download;
    logic cgb_boot_download;
    logic ioctl_wr;
    logic [24:0] ioctl_addr;
    logic [15:0] ioctl_dout;

    logic [23:0] loader_memory_addr;
    logic loader_memory_write;
    logic [63:0] loader_memory_wdata;
    logic [7:0] loader_memory_wstrb;
    logic loader_memory_valid;
    logic loader_memory_ready;
    logic [63:0] loader_memory_rdata;
    logic loader_memory_rvalid;

    logic ce_cpu;
    logic ce_cpu_n;
    logic ce_cpu_2x;
    logic [2:0] clock_phase;
    logic memory_stall;

    logic [7:0] buttons;
    logic [1:0] joy_p54;
    logic [3:0] joy_din;

    logic [14:0] cart_addr;
    logic cart_a15;
    logic cart_rd;
    logic cart_wr;
    logic [7:0] cart_do;
    logic [7:0] cart_di;
    logic cart_oe;
    logic cart_ncs;
    logic sdram_rd;
    logic [22:0] mbc_addr;
    logic is_cram_addr;
    logic cram_rd;
    logic cram_wr;
    logic cram_bram_wr;
    logic [16:0] cram_addr;
    logic [7:0] cart_memory_data;
    logic [23:0] core_memory_addr;
    logic core_memory_read;
    logic core_memory_write;
    logic cart_ready;
    logic cart_is_gbc;
    logic cart_is_sgb;
    logic [7:0] cart_ram_size;
    logic cart_has_save;
    logic cart_bram_save;
    logic speed;
    logic dma_on;

    logic lcd_clkena;
    logic [14:0] lcd_data;
    logic [1:0] lcd_data_gb;
    logic [1:0] lcd_mode;
    logic lcd_on;
    logic lcd_vsync;
    logic [15:0] raw_audio_l;
    logic [15:0] raw_audio_r;
    integer unsigned audio_phase;

    logic ce_32k;
    logic [9:0] ce_32k_div;

    logic [63:0] savestate_ext_din;
    logic [9:0] savestate_ext_addr;
    logic savestate_ext_wren;
    logic savestate_ext_reset;
    logic [63:0] savestate_ext_dout;
    logic savestate_load;
    logic savestate_override;
    logic [19:0] savestate_cram_addr;
    logic savestate_cram_read;
    logic savestate_cram_write;
    logic [15:0] savestate_cram_wdata;
    logic [15:0] savestate_cram_rdata;

    assign core_id = MT_GBC_CORE_ID;
    assign machine_reset = reset || core_reset_request || load_valid ||
                           load_active || !cart_loaded || !cart_ready ||
                           memory_fault;
    assign ready = !machine_reset && !load_error;

    multitini_gameboy_loader loader_i (
        .clk(core_clk), .reset(reset), .clear_error(clear_load_error),
        .load_data(load_data), .load_valid(load_valid),
        .load_ready(load_ready), .load_first(load_first),
        .load_last(load_last), .load_kind(load_kind),
        .load_offset(load_offset), .load_total(load_total),
        .ioctl_download(ioctl_download), .cart_download(cart_download),
        .cgb_boot_download(cgb_boot_download), .ioctl_wr(ioctl_wr),
        .ioctl_addr(ioctl_addr), .ioctl_dout(ioctl_dout),
        .memory_addr(loader_memory_addr),
        .memory_write(loader_memory_write),
        .memory_wdata(loader_memory_wdata),
        .memory_wstrb(loader_memory_wstrb),
        .memory_valid(loader_memory_valid),
        .memory_ready(loader_memory_ready),
        .memory_rdata(loader_memory_rdata),
        .memory_rvalid(loader_memory_rvalid),
        .cart_loaded(cart_loaded),
        .boot_override_loaded(boot_override_loaded),
        .cart_size(cart_size), .active(load_active), .error(load_error)
    );

    multitini_gameboy_clock_enable clock_enable_i (
        .clk(core_clk), .reset(reset), .stall(memory_stall),
        .ce_cpu(ce_cpu), .ce_cpu_n(ce_cpu_n), .ce_cpu_2x(ce_cpu_2x),
        .phase(clock_phase)
    );

    multitini_gameboy_input input_i (
        .clk(core_clk), .reset(machine_reset),
        .input_code(input_code), .input_value(input_value),
        .input_valid(input_valid), .input_ready(input_ready),
        .p14(joy_p54[0]), .p15(joy_p54[1]),
        .joy_din(joy_din), .buttons(buttons)
    );

    assign core_memory_addr = is_cram_addr ?
        MT_GB_MEMORY_CART_RAM_BASE + {7'd0, cram_addr} :
        MT_GB_MEMORY_ROM_BASE + {1'b0, mbc_addr};
    assign core_memory_read = ready && sdram_rd &&
                              !(is_cram_addr && cart_bram_save);
    assign core_memory_write = ready && cram_wr && !cart_bram_save;

    multitini_gameboy_memory memory_i (
        .clk(core_clk), .reset(reset), .loader_active(load_active),
        .loader_addr(loader_memory_addr),
        .loader_write(loader_memory_write),
        .loader_wdata(loader_memory_wdata),
        .loader_wstrb(loader_memory_wstrb),
        .loader_valid(loader_memory_valid),
        .loader_ready(loader_memory_ready),
        .loader_rdata(loader_memory_rdata),
        .loader_rvalid(loader_memory_rvalid),
        .core_read(core_memory_read), .core_write(core_memory_write),
        .core_addr(core_memory_addr), .core_wdata(cart_di),
        .core_rdata(cart_memory_data), .core_stall(memory_stall),
        .memory_addr(memory_addr), .memory_write(memory_write),
        .memory_wdata(memory_wdata), .memory_wstrb(memory_wstrb),
        .memory_valid(memory_valid), .memory_ready(memory_ready),
        .memory_rdata(memory_rdata), .memory_rvalid(memory_rvalid)
    );

    always_ff @(posedge core_clk) begin
        if (reset) begin
            ce_32k_div <= 10'd0;
            ce_32k <= 1'b0;
        end else begin
            ce_32k <= ce_32k_div == 10'd1023;
            ce_32k_div <= ce_32k_div + 10'd1;
        end
    end

    cart_top cart_i (
        .reset(machine_reset), .clk_sys(core_clk),
        .ce_cpu(ce_cpu), .ce_cpu2x(ce_cpu_2x), .speed(speed),
        .megaduck(1'b0), .duck_md0_mode(1'b0), .mapper_sel(3'd0),
        .cart_addr(cart_addr), .cart_a15(cart_a15),
        .cart_rd(cart_rd), .cart_wr(cart_wr), .cart_do(cart_do),
        .cart_di(cart_di), .cart_oe(cart_oe), .nCS(cart_ncs),
        .mbc_addr(mbc_addr), .dn_write(), .cart_ready(cart_ready),
        .is_cram_addr(is_cram_addr), .cram_rd(cram_rd),
        .cram_wr(cram_wr), .cram_bram_wr(cram_bram_wr),
        .cram_addr(cram_addr), .cart_download(cart_download),
        .ram_mask_file(), .ram_size(cart_ram_size),
        .has_save(cart_has_save), .bram_save(cart_bram_save),
        .isGBC_game(cart_is_gbc), .isSGB_game(cart_is_sgb),
        .ioctl_download(ioctl_download), .ioctl_wr(ioctl_wr),
        .ioctl_addr(ioctl_addr), .ioctl_dout(ioctl_dout), .ioctl_wait(),
        .bk_wr(1'b0), .bk_rtc_wr(1'b0), .bk_addr(17'd0),
        .bk_data(16'd0), .bk_q(), .img_size({32'd0, cart_size}),
        .rom_di(cart_memory_data), .joystick_analog_0(16'h8080),
        .ce_32k(ce_32k), .RTC_time(33'd0),
        .RTC_timestampOut(), .RTC_savedtimeOut(), .RTC_inuse(),
        .SaveStateExt_Din(savestate_ext_din),
        .SaveStateExt_Adr(savestate_ext_addr),
        .SaveStateExt_wren(savestate_ext_wren),
        .SaveStateExt_rst(savestate_ext_reset),
        .SaveStateExt_Dout(savestate_ext_dout),
        .savestate_load(savestate_load),
        .savestate_ovr(savestate_override),
        .Savestate_CRAMAddr(savestate_cram_addr),
        .Savestate_CRAMRWrEn(savestate_cram_write),
        .Savestate_CRAMWriteData(savestate_cram_wdata),
        .Savestate_CRAM_Q(savestate_cram_rdata), .rumbling()
    );

    gb core_i (
        .reset(machine_reset), .clk_sys(core_clk),
        .ce(ce_cpu), .ce_n(ce_cpu_n), .ce_2x(ce_cpu_2x),
        .joystick(buttons), .isGBC(1'b1), .real_cgb_boot(1'b0),
        .isSGB(1'b0), .extra_spr_en(1'b0),
        .ext_bus_addr(cart_addr), .ext_bus_a15(cart_a15),
        .cart_rd(cart_rd), .cart_wr(cart_wr), .cart_do(cart_do),
        .cart_di(cart_di), .cart_oe(cart_oe), .nCS(cart_ncs),
        .PHI(), .sdram_rd(sdram_rd),
        .cgb_boot_download(cgb_boot_download),
        .dmg_boot_download(1'b0), .sgb_boot_download(1'b0),
        .ioctl_wr(ioctl_wr), .ioctl_addr(ioctl_addr),
        .ioctl_dout(ioctl_dout), .boot_gba_en(1'b0),
        .fast_boot_en(1'b0), .audio_l(raw_audio_l),
        .audio_r(raw_audio_r), .audio_no_pops(1'b1),
        .megaduck(1'b0), .lcd_clkena(lcd_clkena),
        .lcd_data(lcd_data), .lcd_data_gb(lcd_data_gb),
        .lcd_mode(lcd_mode), .lcd_on(lcd_on), .lcd_vsync(lcd_vsync),
        .joy_p54(joy_p54), .joy_din(joy_din),
        .speed(speed), .DMA_on(dma_on),
        .gg_reset(machine_reset), .gg_en(1'b0), .gg_code(129'd0),
        .gg_available(), .sc_int_clock2(), .serial_clk_in(1'b1),
        .serial_clk_out(), .serial_data_in(1'b1), .serial_data_out(),
        .increaseSSHeaderCount(1'b0), .cart_ram_size(cart_ram_size),
        .save_state(1'b0), .load_state(1'b0), .savestate_number(2'd0),
        .sleep_savestate(), .savestate_ovr(savestate_override),
        .SaveStateExt_Din(savestate_ext_din),
        .SaveStateExt_Adr(savestate_ext_addr),
        .SaveStateExt_wren(savestate_ext_wren),
        .SaveStateExt_rst(savestate_ext_reset),
        .SaveStateExt_Dout(savestate_ext_dout),
        .SaveStateExt_load(savestate_load),
        .Savestate_CRAMAddr(savestate_cram_addr),
        .Savestate_CRAMRdEn(savestate_cram_read),
        .Savestate_CRAMRWrEn(savestate_cram_write),
        .Savestate_CRAMWriteData(savestate_cram_wdata),
        .Savestate_CRAMReadData(savestate_cram_rdata),
        .SAVE_out_Din(), .SAVE_out_Dout(64'd0), .SAVE_out_Adr(),
        .SAVE_out_rnw(), .SAVE_out_ena(), .SAVE_out_be(),
        .SAVE_out_done(1'b0), .savestate_sdram_busy(1'b0),
        .rewind_on(1'b0), .rewind_active(1'b0)
    );

    assign video_r = {lcd_data[4:0], lcd_data[4:2]};
    assign video_g = {lcd_data[9:5], lcd_data[9:7]};
    assign video_b = {lcd_data[14:10], lcd_data[14:12]};
    assign video_de = ready && lcd_on && lcd_clkena;
    assign video_hsync = lcd_mode == 2'd0;
    assign video_vsync = lcd_mode == 2'd1;
    assign video_valid = ready && lcd_clkena;

    always_ff @(posedge core_clk) begin
        audio_valid <= 1'b0;
        if (machine_reset) begin
            audio_phase <= 0;
            audio_l <= 24'sd0;
            audio_r <= 24'sd0;
        end else if (audio_phase + AUDIO_SAMPLE_HZ >= MT_GBC_CORE_HZ) begin
            audio_phase <= audio_phase + AUDIO_SAMPLE_HZ - MT_GBC_CORE_HZ;
            audio_l <= $signed({{8{raw_audio_l[15]}}, raw_audio_l});
            audio_r <= $signed({{8{raw_audio_r[15]}}, raw_audio_r});
            audio_valid <= 1'b1;
        end else begin
            audio_phase <= audio_phase + AUDIO_SAMPLE_HZ;
        end
    end

    wire unused_status = ^{boot_override_loaded, clock_phase, cram_rd,
                           cram_bram_wr, cart_has_save, cart_is_gbc,
                           cart_is_sgb, dma_on, lcd_data_gb, lcd_vsync,
                           savestate_cram_read};
endmodule

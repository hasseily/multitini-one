// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// PAL/NTSC build-time adapter around the pinned FPGA64 machine core. The first
// drive boundary implements one transactional-D64/read-only-G64 serial-IEC
// 1541.
module multitini_c64_adapter #(
    parameter bit NTSC_MODE = 1'b0,
    parameter integer AUDIO_SAMPLE_HZ = 48000,
    // Early and most classic C64 software targets the MOS 6581. Keep the
    // MiSTer core's power-on model at 6581 unless a build explicitly selects
    // another model for both emulated SID instances.
    parameter logic [1:0] SID_VERSION = 2'b00
) (
    input  logic        core_clk,
    input  logic        reset,
    input  logic        core_reset_request,
    input  logic        pause_request,
    input  logic        cpu_turbo_enable,
    input  logic        drive_parallel_enable,
    input  logic [2:0]  sid_mode,
    input  logic        reu_enable,
    input  logic        load_abort,
    input  logic        load_abort_epoch,
    input  logic        clear_load_error,

    input  logic [15:0] input_code,
    input  logic [15:0] input_value,
    input  logic        input_valid,
    output logic        input_ready,

    input  logic [7:0]  load_data,
    input  logic        load_valid,
    output logic        load_ready,
    input  logic        load_first,
    input  logic        load_last,
    input  logic [7:0]  load_kind,
    input  logic [31:0] load_offset,
    input  logic [31:0] load_total,
    input  logic        load_epoch,

    output logic [23:0] memory_addr,
    output logic        memory_write,
    output logic [63:0] memory_wdata,
    output logic [7:0]  memory_wstrb,
    output logic        memory_valid,
    input  logic        memory_ready,
    input  logic [63:0] memory_rdata,
    input  logic        memory_rvalid,
    input  logic        memory_fault,

    // Cartridge traffic above remains on PSRAM; REU has an independent DDR
    // aperture and cannot consume or overwrite cartridge storage.
    output logic [23:0] reu_memory_addr,
    output logic        reu_memory_write,
    output logic [63:0] reu_memory_wdata,
    output logic [7:0]  reu_memory_wstrb,
    output logic        reu_memory_valid,
    output logic        reu_memory_burst,
    output logic        reu_memory_busy,
    input  logic        reu_memory_ready,
    input  logic [63:0] reu_memory_rdata,
    input  logic        reu_memory_rvalid,
    input  logic        reu_memory_fault,

    input  logic        media_mount,
    input  logic        media_eject,
    input  logic        media_complete,
    input  logic [7:0]  media_slot,
    input  logic [7:0]  media_kind,
    input  logic        media_read_only,
    input  logic [31:0] media_image_size,
    input  logic [31:0] media_generation,
    input  logic [31:0] media_complete_request_id,
    input  logic [31:0] media_complete_status,
    input  logic [15:0] media_complete_block_count,
    input  logic [31:0] media_complete_generation,

    output logic        media_request_valid,
    output logic        media_request_write,
    output logic [31:0] media_request_id,
    output logic [31:0] media_request_lba,
    output logic [15:0] media_request_block_count,
    output logic [31:0] media_request_generation,

    output logic [7:0]  media_write_data,
    output logic        media_write_data_valid,
    output logic        media_write_data_last,
    input  logic        media_write_data_ready,

    output logic        media_state_mounted,
    output logic        media_state_dirty,
    output logic        media_state_eject_pending,

    output logic [31:0] core_id,
    output logic        ready,
    output logic        load_error,
    output logic [2:0]  rom_loaded,
    output logic        prg_loaded,
    output logic        cart_loaded,
    output logic [7:0]  cart_hardware_type,
    output logic        prg_run_busy,

    output logic [7:0]  video_r,
    output logic [7:0]  video_g,
    output logic [7:0]  video_b,
    output logic        video_de,
    output logic        video_hsync,
    output logic        video_vsync,
    output logic        video_valid,

    output logic signed [23:0] audio_l,
    output logic signed [23:0] audio_r,
    output logic               audio_valid
);
    import multitini_c64_defs_pkg::*;

    localparam integer CORE_CLOCK_HZ = NTSC_MODE ? MT_C64_NTSC_HZ : MT_C64_PAL_HZ;

    logic        rom_wr;
    logic        rom_bank;
    logic [13:0] rom_addr;
    logic [7:0]  rom_data;
    logic        roms_ready;
    logic        rom_load_active;
    logic        content_load_active;
    logic        system_rom_load_valid;
    logic        content_load_valid;
    logic        rom_load_valid;
    logic        rom_load_ready;
    logic        rom_load_error;
    logic        content_load_ready;
    logic        content_load_error;
    logic        fatal_load_error;
    logic        content_core_reset_hold;
    logic        content_load_resets_run_injector;
    logic        prg_install_pause;
    logic        core_paused;
    logic        d64_load_valid;
    logic        d64_load_ready;
    logic        g64_load_valid;
    logic        g64_load_ready;
    logic        drive_media_error;
    logic        drive_rom_wr;
    logic [13:0] drive_rom_addr;
    logic [7:0]  drive_rom_data;
    logic        drive_rom_loaded;
    logic        core_enable;
    logic        core_reset_n;
    logic        drive_soft_reset;

    logic        prg_ram_wr;
    logic [15:0] prg_ram_addr;
    logic [7:0]  prg_ram_data;
    logic        prg_run_request;

    logic [23:0] loader_memory_addr;
    logic        loader_memory_write;
    logic [63:0] loader_memory_wdata;
    logic [7:0]  loader_memory_wstrb;
    logic        loader_memory_valid;
    logic        loader_memory_ready;
    logic        cart_loading;
    logic        cart_exrom_header;
    logic        cart_game_header;
    logic        cart_bank_hi;
    logic        cart_bank_16k;
    logic [7:0]  cart_bank_num;
    logic [7:0]  cart_bank_addr;
    logic        cart_bank_wr;
    logic        cart_memory_fault;
    logic        cart_memory_busy;
    logic        cart_loader_busy;
    logic [7:0]  loader_cart_id;

    logic        c64_iec_data;
    logic        c64_iec_clk;
    logic        c64_iec_atn;
    logic        drive_iec_data;
    logic        drive_iec_clk;
    logic        drive_mounted;
    logic        drive_led;
    logic        cpu_turbo_active;
    logic [7:0]  c64_pb_o;
    logic [7:0]  c64_pb_oe;
    logic        c64_pc2_n;
    logic [7:0]  drive_par_data_out;
    logic        drive_par_stb_out;

    logic [10:0] host_ps2_key;
    logic        host_input_ready;
    logic [10:0] ps2_key;
    logic [6:0]  joy_a;
    logic [6:0]  joy_b;

    logic [15:0] ram_addr;
    wire [7:0]   main_ram_din;
    logic [7:0]  core_ram_din;
    logic [7:0]  ram_dout;
    logic        ram_ce;
    logic        ram_we;

    logic        cart_game;
    logic        cart_exrom;
    logic        cart_rom_l;
    logic        cart_rom_h;
    logic        cart_umax_rom_h;
    logic        cart_ioe;
    logic        cart_iof;
    logic        cart_io_rom;
    logic        cart_io_read;
    logic [7:0]  cart_io_data;
    logic        cart_nmi;
    logic        cart_nmi_ack;
    logic        cart_mem_ce;
    logic        cart_mem_we;
    logic [7:0]  cart_mem_in;
    logic [7:0]  cart_mem_out;
    logic [7:0]  cart_data_out;
    logic [24:0] cart_mem_addr;
    logic        cart_mem_req;
    logic        cart_mem_cycle;
    logic [7:0]  cart_runtime_data;
    logic        cart_external_select;
    logic        cart_runtime_request;
    logic        cart_stall;
    logic [23:0] cart_memory_addr;
    logic cart_memory_write;
    logic [63:0] cart_memory_wdata;
    logic [7:0] cart_memory_wstrb;
    logic cart_memory_valid;
    logic cart_memory_ready;
    logic cart_memory_rvalid;
    logic reu_active, reu_irq, reu_busy, reu_fault;
    logic reu_cache_busy, reu_cache_fault;
    logic [23:0] reu_request_addr, reu_hint_addr;
    logic reu_request_write, reu_request_valid, reu_request_ready, reu_response_valid, reu_hint_read;
    logic [63:0] reu_request_wdata, reu_response_rdata;
    logic [7:0] reu_request_wstrb;
    logic reu_fault_latched;
    logic reu_dma_req, reu_dma_valid, reu_dma_ack, reu_dma_write;
    logic [15:0] reu_dma_addr, cpu_bus_addr;
    logic [7:0] reu_dma_wdata, reu_dma_rdata, cpu_bus_wdata, reu_io_rdata;
    logic cpu_bus_valid, cpu_bus_write;
    logic reu_io_select;

    logic        raw_hsync;
    logic        raw_vsync;
    logic [7:0]  raw_r;
    logic [7:0]  raw_g;
    logic [7:0]  raw_b;
    logic        sync_hsync;
    logic        sync_vsync;
    logic        hblank;
    logic        vblank;
    logic        sync_pixel_tick;
    logic        sync_pixel_tick_d;

    logic [17:0] raw_audio_l;
    logic [17:0] raw_audio_r;
    integer unsigned audio_phase;

    assign core_id = MT_C64_CORE_ID;
    assign system_rom_load_valid = load_valid &&
        (load_kind == MT_LOAD_C64_BASIC ||
         load_kind == MT_LOAD_C64_KERNAL ||
         load_kind == MT_LOAD_C64_CHARACTER ||
         load_kind == MT_LOAD_C64_1541_ROM);
    assign content_load_valid = load_valid &&
        (load_kind == MT_LOAD_C64_PRG ||
         load_kind == MT_LOAD_C64_PRG_STAGE ||
         load_kind == MT_LOAD_C64_PRG_RUN ||
         load_kind == MT_LOAD_C64_CRT ||
         load_kind == MT_LOAD_C64_CRT_DETACH);
    assign content_load_resets_run_injector =
        content_load_valid && load_first &&
        load_kind != MT_LOAD_C64_PRG_RUN;
    assign rom_load_valid = system_rom_load_valid;
    assign d64_load_valid = load_valid && load_kind == MT_LOAD_C64_D64;
    assign g64_load_valid = load_valid && load_kind == MT_LOAD_C64_G64;
    // The asynchronous mailbox presents its payload while valid is stalled, so
    // D64 bytes can apply drive-buffer backpressure without blocking a later
    // ROM object whose kind selects the ordinary loader instead.
    always_comb begin
        case (load_kind)
            MT_LOAD_C64_D64: load_ready = d64_load_ready;
            MT_LOAD_C64_G64: load_ready = g64_load_ready;
            MT_LOAD_C64_PRG,
            MT_LOAD_C64_PRG_STAGE,
            MT_LOAD_C64_PRG_RUN,
            MT_LOAD_C64_CRT,
            MT_LOAD_C64_CRT_DETACH: load_ready = content_load_ready;
            default: load_ready = rom_load_ready;
        endcase
    end
    assign load_error = rom_load_error || content_load_error ||
                        drive_media_error || cart_memory_fault || reu_fault_latched;
    assign fatal_load_error = rom_load_error || content_load_error ||
                              cart_memory_fault || reu_fault_latched;
    assign ready = core_enable && !reset && !core_reset_request &&
                   !system_rom_load_valid && !content_load_valid &&
                   !rom_load_active && !content_load_active &&
                   !fatal_load_error &&
                   !cart_loader_busy && !prg_run_request && !prg_run_busy;
    assign core_reset_n = core_enable && !reset && !core_reset_request &&
                          !system_rom_load_valid && !rom_load_active &&
                          !content_core_reset_hold &&
                          !cart_loader_busy && !fatal_load_error;
    assign drive_soft_reset = core_reset_request || !core_enable;
    assign cart_hardware_type = cart_loaded ? loader_cart_id : 8'hff;

    multitini_c64_loader loader_i (
        .clk(core_clk), .reset(reset), .clear_error(clear_load_error),
        .load_data(load_data), .load_valid(rom_load_valid),
        .load_ready(rom_load_ready),
        .load_first(load_first), .load_last(load_last), .load_kind(load_kind),
        .load_offset(load_offset), .load_total(load_total),
        .rom_wr(rom_wr), .rom_bank(rom_bank), .rom_addr(rom_addr),
        .rom_data(rom_data), .rom_loaded(rom_loaded),
        .drive_rom_wr(drive_rom_wr), .drive_rom_addr(drive_rom_addr),
        .drive_rom_data(drive_rom_data),
        .drive_rom_loaded(drive_rom_loaded),
        .roms_ready(roms_ready), .active(rom_load_active),
        .error(rom_load_error)
    );

    multitini_c64_content_loader content_loader_i (
        .clk(core_clk), .reset(reset), .load_abort(load_abort),
        .load_abort_epoch(load_abort_epoch),
        .clear_error(clear_load_error),
        .load_data(load_data), .load_valid(content_load_valid),
        .load_ready(content_load_ready), .load_first(load_first),
        .load_last(load_last), .load_kind(load_kind),
        .load_offset(load_offset), .load_total(load_total),
        .load_epoch(load_epoch),
        .core_addr(ram_addr),
        .prg_ram_wr(prg_ram_wr), .prg_ram_addr(prg_ram_addr),
        .prg_ram_data(prg_ram_data), .prg_loaded(prg_loaded),
        .prg_run_request(prg_run_request),
        .loader_memory_addr(loader_memory_addr),
        .loader_memory_write(loader_memory_write),
        .loader_memory_wdata(loader_memory_wdata),
        .loader_memory_wstrb(loader_memory_wstrb),
        .loader_memory_valid(loader_memory_valid),
        .loader_memory_ready(loader_memory_ready),
        .memory_fault(cart_memory_fault),
        .cart_loading(cart_loading), .cart_loaded(cart_loaded),
        .cart_id(loader_cart_id), .cart_exrom(cart_exrom_header),
        .cart_game(cart_game_header), .cart_bank_hi(cart_bank_hi),
        .cart_bank_16k(cart_bank_16k), .cart_bank_num(cart_bank_num),
        .cart_bank_addr(cart_bank_addr), .cart_bank_wr(cart_bank_wr),
        .core_reset_hold(content_core_reset_hold),
        .prg_install_pause(prg_install_pause),
        .active(content_load_active), .error(content_load_error)
    );

    multitini_c64_input input_i (
        .clk(core_clk), .reset(!core_reset_n),
        .input_code(input_code), .input_value(input_value),
        .input_valid(input_valid && !prg_run_busy),
        .input_ready(host_input_ready),
        .ps2_key(host_ps2_key), .joy_a(joy_a), .joy_b(joy_b)
    );
    assign input_ready = host_input_ready && !prg_run_busy;

    multitini_c64_run_injector run_injector_i (
        .clk(core_clk),
        .reset(reset || core_reset_request ||
               content_load_resets_run_injector || load_abort),
        .pause_request(pause_request || core_paused),
        .start(prg_run_request), .host_ps2_key(host_ps2_key),
        .ps2_key(ps2_key), .busy(prg_run_busy)
    );

    // Delay reset release until the final registered ROM write has reached
    // the VHDL memory. Any replacement ROM object immediately stops the core.
    always_ff @(posedge core_clk) begin
        if (reset || core_reset_request || !roms_ready || rom_load_active ||
            content_core_reset_hold || system_rom_load_valid ||
            rom_wr || drive_rom_wr)
            core_enable <= 1'b0;
        else
            core_enable <= 1'b1;
    end

    multitini_c64_local_ram main_ram_i (
        .clk(core_clk),
        .loader_we(prg_ram_wr), .loader_addr(prg_ram_addr),
        .loader_data(prg_ram_data),
        .machine_ce(ram_ce), .machine_we(ram_we),
        .machine_store(cart_mem_we && !cart_external_select),
        .machine_addr(ram_addr), .machine_data(ram_dout),
        .machine_q(main_ram_din)
    );

    assign cart_external_select = cart_loaded && cart_mem_ce &&
                                  cart_mem_addr[24:16] != 9'd0;
    assign cart_runtime_request = core_reset_n && cart_external_select;
    assign cart_mem_in = cart_external_select ?
                         cart_runtime_data : main_ram_din;
    assign core_ram_din = cart_loaded ? cart_data_out : main_ram_din;

    multitini_c64_cart_memory cart_memory_i (
        .clk(core_clk), .reset(reset), .loader_active(cart_loading),
        .loader_addr(loader_memory_addr),
        .loader_write(loader_memory_write),
        .loader_wdata(loader_memory_wdata),
        .loader_wstrb(loader_memory_wstrb),
        .loader_valid(loader_memory_valid),
        .loader_ready(loader_memory_ready),
        .core_request(cart_runtime_request), .core_write(cart_mem_we),
        .core_addr(cart_mem_addr[23:0]), .core_wdata(cart_mem_out),
        .core_rdata(cart_runtime_data),
        .core_stall(cart_stall), .busy(cart_memory_busy),
        .loader_busy(cart_loader_busy),
        .fault(cart_memory_fault), .clear_fault(clear_load_error),
        .memory_addr(cart_memory_addr), .memory_write(cart_memory_write),
        .memory_wdata(cart_memory_wdata), .memory_wstrb(cart_memory_wstrb),
        .memory_valid(cart_memory_valid), .memory_ready(cart_memory_ready),
        .memory_rdata(memory_rdata), .memory_rvalid(cart_memory_rvalid),
        .memory_fault(memory_fault)
    );

    // Keep the existing cartridge exclusion until simultaneous IO2 mapping
    // has a defined compatibility policy. Storage is now independent DDR.
    assign reu_active = reu_enable && !cart_loaded && !cart_loading;
    // MAME releases the REU register aperture during its own DMA. A transfer
    // reading $DFxx observes the machine's floating bus, not REU status.
    assign reu_io_select = reu_active && !reu_dma_req && cart_iof;
    always_ff @(posedge core_clk) begin
        if (reset || clear_load_error) reu_fault_latched <= 1'b0;
        else if (reu_fault || reu_cache_fault) reu_fault_latched <= 1'b1;
    end
    multitini_c64_reu reu_i (
        .clk(core_clk), .reset(reset || !core_reset_n), .enable(reu_active),
        .io_valid(cpu_bus_valid && reu_io_select), .io_addr(cpu_bus_addr[4:0]),
        .io_write(cpu_bus_write), .io_wdata(cpu_bus_wdata), .io_rdata(reu_io_rdata),
        .ff00_write(cpu_bus_valid && cpu_bus_write && cpu_bus_addr == 16'hff00),
        .dma_req(reu_dma_req), .dma_valid(reu_dma_valid), .dma_addr(reu_dma_addr),
        .dma_wdata(reu_dma_wdata), .dma_write(reu_dma_write), .dma_ack(reu_dma_ack),
        .dma_rdata(reu_dma_rdata), .irq(reu_irq), .busy(reu_busy), .fault(reu_fault),
        .memory_addr(reu_request_addr), .memory_write(reu_request_write),
        .memory_wdata(reu_request_wdata), .memory_wstrb(reu_request_wstrb),
        .memory_valid(reu_request_valid), .memory_ready(reu_request_ready),
        .memory_hint_addr(reu_hint_addr), .memory_hint_read(reu_hint_read),
        .memory_rdata(reu_response_rdata), .memory_rvalid(reu_response_valid),
        .memory_fault(reu_cache_fault)
    );
    // Guest reset/disable stops speculation, but accepted writes survive and
    // drain before the shell reports DDR quiescence for PCAP or storage handoff.
    multitini_c64_reu_cache reu_cache_i (
        .clk(core_clk), .reset(reset), .enable(reu_active && core_reset_n), .flush(!reu_busy),
        .hint_addr(reu_hint_addr), .hint_read(reu_hint_read),
        .request_addr(reu_request_addr), .request_write(reu_request_write),
        .request_wdata(reu_request_wdata), .request_wstrb(reu_request_wstrb),
        .request_valid(reu_request_valid), .request_ready(reu_request_ready),
        .response_rdata(reu_response_rdata), .response_valid(reu_response_valid),
        .memory_addr(reu_memory_addr), .memory_write(reu_memory_write),
        .memory_wdata(reu_memory_wdata), .memory_wstrb(reu_memory_wstrb),
        .memory_burst(reu_memory_burst), .memory_valid(reu_memory_valid),
        .memory_ready(reu_memory_ready), .memory_rdata(reu_memory_rdata),
        .memory_rvalid(reu_memory_rvalid), .memory_fault(reu_memory_fault),
        .busy(reu_cache_busy), .fault(reu_cache_fault)
    );
    assign memory_addr = cart_memory_addr;
    assign memory_write = cart_memory_write;
    assign memory_wdata = cart_memory_wdata;
    assign memory_wstrb = cart_memory_wstrb;
    assign memory_valid = cart_memory_valid;
    assign cart_memory_ready = memory_ready;
    assign cart_memory_rvalid = memory_rvalid;
    assign reu_memory_busy = reu_busy || reu_cache_busy;

    cartridge cartridge_i (
        .clk32(core_clk), .reset_n(core_reset_n),
        .cart_loading(cart_loading),
        .cart_id(cart_loaded ? loader_cart_id : 8'hff),
        .cart_exrom(cart_exrom_header), .cart_game(cart_game_header),
        .cart_bank_hi(cart_bank_hi), .cart_bank_16k(cart_bank_16k),
        .cart_bank_num(cart_bank_num), .cart_bank_addr(cart_bank_addr),
        .cart_bank_wr(cart_bank_wr), .cart_boot(1'b1),
        .exrom(cart_exrom), .game(cart_game), .romL(cart_rom_l),
        .romH(cart_rom_h), .UMAXromH(cart_umax_rom_h),
        .IOE(cart_ioe), .IOF(cart_iof), .mem_write(ram_we),
        .mem_ce(ram_ce), .mem_ce_out(cart_mem_ce),
        .mem_write_out(cart_mem_we), .mem_in(cart_mem_in),
        .mem_out(cart_mem_out), .mem_addr(cart_mem_addr),
        .mem_req(cart_mem_req), .mem_cycle(cart_mem_cycle),
        .IO_rom(cart_io_rom), .IO_rd(cart_io_read),
        .IO_data(cart_io_data), .addr_in(ram_addr), .data_in(ram_dout),
        .data_out(cart_data_out), .freeze_key(1'b0),
        .mod_key(1'b0), .nmi(cart_nmi), .nmi_ack(cart_nmi_ack)
    );

    fpga64_sid_iec core_i (
        .clk32(core_clk), .reset_n(core_reset_n),
        .bios({1'b0, rom_bank}),
        // Menu visibility and PRG installation use FPGA64's boundary-safe
        // whole-core pause. The acknowledged pause also stops the drive.
        // Cartridge PSRAM misses instead stretch the active VIC/CPU bus phase
        // until the requested cache line is present.
        .pause(prg_install_pause || pause_request), .memory_wait(cart_stall),
        .pause_out(core_paused),
        .ps2_key(ps2_key), .kbd_reset(!core_reset_n), .shift_mod(2'b11),
        .ramAddr(ram_addr), .ramDin(core_ram_din), .ramDout(ram_dout),
        .ramCE(ram_ce), .ramWE(ram_we),
        .io_cycle(cart_mem_cycle), .ext_cycle(), .refresh(),
        // FPGA64 Smart turbo adds CPU RAM/ROM cycles while CIA/IEC and VIC
        // remain at 1x. The independent setting is automatically suspended
        // around IEC traffic, matching the pinned upstream integration.
        .cia_mode(1'b0), .turbo_mode({cpu_turbo_active, 1'b0}),
        .turbo_speed(2'b00),
        .local_turbo_allowed(!cart_loaded),
        .vic_variant(2'b00), .ntscMode(NTSC_MODE),
        .hsync(raw_hsync), .vsync(raw_vsync), .palette(3'b000),
        .r(raw_r), .g(raw_g), .b(raw_b),
        .game(cart_game), .exrom(cart_exrom), .io_rom(cart_io_rom),
        .io_ext(cart_io_read || reu_io_select),
        .io_data(reu_io_select ? reu_io_rdata : cart_io_data), .irq_n(1'b1),
        .nmi_n(!cart_nmi), .nmi_ack(cart_nmi_ack),
        .romL(cart_rom_l), .romH(cart_rom_h),
        .UMAXromH(cart_umax_rom_h), .IOE(cart_ioe), .IOF(cart_iof),
        .freeze_key(), .mod_key(),
        .tape_play(), .tape_rew(),
        .tape_stop(), .tape_ff(), .tape_reset_counter(),
        .dma_req(reu_dma_req), .dma_valid(reu_dma_valid), .dma_ack(reu_dma_ack),
        .cpu_bus_valid(cpu_bus_valid), .cpu_bus_addr(cpu_bus_addr),
        .cpu_bus_wdata(cpu_bus_wdata), .cpu_bus_write(cpu_bus_write),
        .dma_cycle(), .dma_addr(reu_dma_addr),
        .dma_dout(reu_dma_wdata), .dma_din(reu_dma_rdata),
        .dma_we(reu_dma_write), .irq_ext_n(!reu_irq),
        .joyA(joy_a), .joyB(joy_b),
        .pot1(8'hFF), .pot2(8'hFF), .pot3(8'hFF), .pot4(8'hFF),
        .audio_l(raw_audio_l), .audio_r(raw_audio_r),
        .sid_filter(2'b11), .sid_ver(SID_VERSION), .sid_mode(sid_mode),
        .sid_cfg(4'b0000), .sid_fc_off_l(13'd0), .sid_fc_off_r(13'd0),
        .sid_ld_clk(core_clk), .sid_ld_addr(12'd0), .sid_ld_data(16'd0),
        .sid_ld_wr(1'b0), .sid_digifix(1'b0),
        // User port: the opt-in DolphinDOS-style parallel cable to the drive.
        // The drive strobe is both a FLAG2 edge and a level on PA2.
        .pb_i(drive_par_data_out), .pb_o(c64_pb_o), .pb_oe_o(c64_pb_oe),
        .pa2_i(drive_par_stb_out), .pa2_o(), .pc2_n_o(c64_pc2_n),
        .flag2_n_i(drive_par_stb_out), .sp2_i(1'b1), .sp2_o(),
        .sp1_i(1'b1), .sp1_o(), .cnt2_i(1'b1), .cnt2_o(),
        .cnt1_i(1'b1), .cnt1_o(),
        .iec_data_o(c64_iec_data), .iec_data_i(drive_iec_data),
        .iec_clk_o(c64_iec_clk), .iec_clk_i(drive_iec_clk),
        .iec_atn_o(c64_iec_atn),
        .c64rom_addr(rom_addr), .c64rom_data(rom_data), .c64rom_wr(rom_wr),
        .cass_motor(), .cass_write(), .cass_sense(1'b1), .cass_read(1'b1)
    );

    multitini_c64_cpu_turbo_guard #(.CORE_CLOCK_HZ(CORE_CLOCK_HZ))
        cpu_turbo_guard_i (
            .clk(core_clk), .reset(reset),
            .turbo_request(cpu_turbo_enable),
            .c64_iec_atn(c64_iec_atn), .c64_iec_data(c64_iec_data),
            .c64_iec_clk(c64_iec_clk), .drive_iec_data(drive_iec_data),
            .drive_iec_clk(drive_iec_clk),
            // Cable strobes count as drive traffic only while the cable is
            // connected; the drive side already idles high otherwise.
            .c64_par_stb(c64_pc2_n || !drive_parallel_enable),
            .drive_par_stb(drive_par_stb_out),
            .turbo_active(cpu_turbo_active)
        );

    multitini_c64_1541 #(.CORE_CLOCK_HZ(CORE_CLOCK_HZ)) drive_i (
        .clk(core_clk), .reset(reset), .soft_reset(drive_soft_reset),
        .pause_request(core_paused),
        .clear_error(clear_load_error),
        .drive_rom_write(drive_rom_wr),
        .drive_rom_address(drive_rom_addr),
        .drive_rom_write_data(drive_rom_data),
        .drive_rom_loaded(drive_rom_loaded),
        .c64_iec_atn(c64_iec_atn), .c64_iec_data(c64_iec_data),
        .c64_iec_clk(c64_iec_clk), .drive_iec_data(drive_iec_data),
        .drive_iec_clk(drive_iec_clk),
        .parallel_enable(drive_parallel_enable),
        // A user-port line reads high unless the C64 drives it low as an
        // output; the CIA exports its output register and data direction.
        .par_data_in(c64_pb_o | ~c64_pb_oe), .par_stb_in(c64_pc2_n),
        .par_data_out(drive_par_data_out), .par_stb_out(drive_par_stb_out),
        .media_mount(media_mount), .media_eject(media_eject),
        .media_complete(media_complete), .media_slot(media_slot),
        .media_kind(media_kind), .media_read_only(media_read_only),
        .media_image_size(media_image_size),
        .media_generation(media_generation),
        .media_complete_request_id(media_complete_request_id),
        .media_complete_status(media_complete_status),
        .media_complete_block_count(media_complete_block_count),
        .media_complete_generation(media_complete_generation),
        .media_request_valid(media_request_valid),
        .media_request_write(media_request_write),
        .media_request_id(media_request_id),
        .media_request_lba(media_request_lba),
        .media_request_block_count(media_request_block_count),
        .media_request_generation(media_request_generation),
        .media_write_data(media_write_data),
        .media_write_data_valid(media_write_data_valid),
        .media_write_data_last(media_write_data_last),
        .media_write_data_ready(media_write_data_ready),
        .d64_load_data(load_data), .d64_load_valid(d64_load_valid),
        .d64_load_ready(d64_load_ready), .d64_load_first(load_first),
        .d64_load_last(load_last), .d64_load_offset(load_offset),
        .d64_load_total(load_total),
        .g64_load_data(load_data), .g64_load_valid(g64_load_valid),
        .g64_load_ready(g64_load_ready), .g64_load_first(load_first),
        .g64_load_last(load_last), .g64_load_offset(load_offset),
        .g64_load_total(load_total), .mounted(drive_mounted),
        .media_dirty(media_state_dirty),
        .media_eject_pending(media_state_eject_pending),
        .drive_led(drive_led), .error(drive_media_error)
    );

    assign media_state_mounted = drive_mounted;

    video_sync sync_i (
        .clk32(core_clk), .pause(!core_reset_n || core_paused),
        .hsync(raw_hsync), .vsync(raw_vsync), .ntsc(NTSC_MODE),
        .wide(1'b0), .hsync_out(sync_hsync), .vsync_out(sync_vsync),
        .hblank(hblank), .vblank(vblank), .pixel_tick(sync_pixel_tick)
    );

    // video_sync locks its divide-by-four pixel phase to the VIC hsync. Wait
    // one complete core-clock edge after that tick before registering the
    // entire video tuple, so RGB, blanking, and sync all describe one stable
    // source pixel when video_valid is asserted.
    always_ff @(posedge core_clk) begin
        if (!core_reset_n) begin
            sync_pixel_tick_d <= 1'b0;
            video_r           <= 8'd0;
            video_g           <= 8'd0;
            video_b           <= 8'd0;
            video_de          <= 1'b0;
            video_hsync       <= 1'b0;
            video_vsync       <= 1'b0;
            video_valid       <= 1'b0;
        end else begin
            sync_pixel_tick_d <= sync_pixel_tick;
            video_valid       <= sync_pixel_tick_d;
            if (sync_pixel_tick_d) begin
                video_r     <= raw_r;
                video_g     <= raw_g;
                video_b     <= raw_b;
                video_de    <= !hblank && !vblank;
                video_hsync <= sync_hsync;
                video_vsync <= sync_vsync;
            end
        end
    end

    // Sample the signed SID outputs at 48 kHz without assuming an integer
    // relationship between the authentic PAL/NTSC core clock and audio rate.
    always_ff @(posedge core_clk) begin
        audio_valid <= 1'b0;
        if (!core_reset_n) begin
            audio_phase <= 0;
            audio_l <= 24'sd0;
            audio_r <= 24'sd0;
        end else if (audio_phase + AUDIO_SAMPLE_HZ >= CORE_CLOCK_HZ) begin
            audio_phase <= audio_phase + AUDIO_SAMPLE_HZ - CORE_CLOCK_HZ;
            // MiSTer consumes bits 17:2 as signed 16-bit PCM. Preserve that
            // full-scale convention at the common 24-bit boundary.
            audio_l <= $signed({raw_audio_l, 6'b000000});
            audio_r <= $signed({raw_audio_r, 6'b000000});
            audio_valid <= 1'b1;
        end else begin
            audio_phase <= audio_phase + AUDIO_SAMPLE_HZ;
        end
    end

    wire unused_drive_status = d64_load_ready ^ drive_mounted ^ drive_led;
endmodule

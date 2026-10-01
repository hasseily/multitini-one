// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Board-facing adapter. The ungated clock owns transport and SRAM snapshots;
// a dedicated clock buffer suspends all chip phases together on a ROM miss.
module multitini_genesis_adapter #(
    parameter bit PAL_MODE=0,
    parameter integer AUDIO_SAMPLE_HZ = 48000
) (
    input  logic         core_clk,
    input  logic         reset,
    input  logic         core_reset_request,
    input  logic         pause_request,
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
    input  logic load_epoch,load_abort,load_abort_epoch,

    output logic [23:0]  memory_addr,
    output logic         memory_write,
    output logic [63:0]  memory_wdata,
    output logic [7:0]   memory_wstrb,
    output logic         memory_valid,
    input  logic         memory_ready,
    input  logic [63:0]  memory_rdata,
    input  logic         memory_rvalid,
    input  logic         memory_fault,

    input  logic         media_mount,
    input  logic         media_eject,
    input  logic         media_complete,
    input  logic [7:0]   media_slot,
    input  logic [7:0]   media_kind,
    input  logic         media_read_only,
    input  logic [31:0]  media_image_size,
    input  logic [31:0]  media_generation,
    input  logic [31:0]  media_complete_request_id,
    input  logic [31:0]  media_complete_status,
    input  logic [15:0]  media_complete_block_count,
    input  logic [31:0]  media_complete_generation,

    output logic         media_request_valid,
    output logic         media_request_write,
    output logic [31:0]  media_request_id,
    output logic [31:0]  media_request_lba,
    output logic [15:0]  media_request_block_count,
    output logic [31:0]  media_request_generation,
    output logic [7:0]   media_write_data,
    output logic         media_write_data_valid,
    output logic         media_write_data_last,
    input  logic         media_write_data_ready,
    output logic         media_state_mounted,
    output logic         media_state_dirty,
    output logic         media_state_eject_pending,

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
    output logic         video_mode_240,
    output logic signed [23:0] audio_l,
    output logic signed [23:0] audio_r,
    output logic                audio_valid
);
    import multitini_genesis_defs_pkg::*;
    localparam integer CLOCK_HZ=PAL_MODE ? MT_GENESIS_CORE_PAL_HZ : MT_GENESIS_CORE_NTSC_HZ;
    wire machine_reset,chip_clk,machine_ready,rom_stall;
    wire cart_download,cart_word_write,config_valid,loader_error,cache_fault,cache_invalidate;
    wire [22:0] cart_word_address;
    wire [15:0] cart_word_data;
    wire [7:0] region;
    wire [1:0] six_button,lowpass;
    wire fm_chip;
    wire [23:0] loader_addr;
    wire [63:0] loader_data;
    wire [7:0] loader_strobe;
    wire loader_valid,loader_ready,loader_response;
    wire [15:0] loader_save_addr;
    wire [7:0] loader_save_data;
    wire loader_save_write;
    wire [23:0] rom_addr;
    wire [15:0] rom_data;
    wire rom_request,rom_cached;
    wire save_pause,save_error,save_changed;
    wire [23:0] save_memory_addr;
    wire save_memory_read,save_memory_ready;
    reg [63:0] save_memory_data;
    reg save_memory_response;
    reg [11:0] buttons0,buttons1;
    wire signed [15:0] chip_audio_l,chip_audio_r;
    wire chip_video_ce;
    assign core_id=MT_GENESIS_CORE_ID;
    assign machine_reset=reset || core_reset_request || !cart_loaded || !config_valid || cache_fault || memory_fault;
    assign ready=!machine_reset && machine_ready && !loader_error;
    assign load_error=loader_error || cache_fault || save_error;
    assign input_ready=1'b1;
    always @(posedge core_clk)begin
        if(reset || core_reset_request)begin buttons0<=0;buttons1<=0;end
        else if(input_valid && input_code[15:8]==MT_INPUT_GENESIS_GAMEPAD)begin
            if(input_code[7:0]==0)buttons0<=input_value[11:0];
            if(input_code[7:0]==1)buttons1<=input_value[11:0];
        end
    end
    multitini_genesis_loader #(.PAL_MODE(PAL_MODE)) loader_i(
        .clk(core_clk),.reset(reset),.core_reset_request(core_reset_request),.clear_error(clear_load_error),
        .load_data(load_data),.load_kind(load_kind),.load_valid(load_valid),.load_ready(load_ready),
        .load_first(load_first),.load_last(load_last),.load_offset(load_offset),.load_total(load_total),
        .load_epoch(load_epoch),.load_abort(load_abort),.load_abort_epoch(load_abort_epoch),
        .error(loader_error),.cart_loaded(cart_loaded),.config_valid(config_valid),.cart_size(cart_size),
        .region(region),.six_button(six_button),.lowpass(lowpass),.fm_chip(fm_chip),
        .cart_download(cart_download),.cart_word_write(cart_word_write),
        .cart_word_address(cart_word_address),.cart_word_data(cart_word_data),
        .save_address(loader_save_addr),.save_data(loader_save_data),.save_write(loader_save_write),
        .cache_invalidate(cache_invalidate),.write_addr(loader_addr),.write_data(loader_data),
        .write_strobe(loader_strobe),.write_valid(loader_valid),.write_ready(loader_ready),
        .write_response(loader_response),.memory_fault(cache_fault || memory_fault));
    // Non-power-of-two cartridge tails are open bus, never previous-cart data.
    wire rom_padding={8'd0,rom_addr}>=cart_size;
    multitini_genesis_rom_cache cache_i(
        .clk(core_clk),.resetn(!reset),.invalidate(cache_invalidate),
        .loading(cart_download || core_reset_request || !cart_loaded),
        .read_addr(rom_addr),.read_valid(rom_request && !rom_cached && !rom_padding && !machine_reset),
        .read_ready(),.cached(rom_cached),.cached_data(rom_data),.read_data(),.read_response(),
        .write_addr(loader_addr),.write_data(loader_data),.write_strobe(loader_strobe),
        .write_valid(loader_valid),.write_ready(loader_ready),.write_response(loader_response),
        .memory_addr(memory_addr),.memory_write(memory_write),.memory_wdata(memory_wdata),
        .memory_wstrb(memory_wstrb),.memory_valid(memory_valid),.memory_ready(memory_ready),
        .memory_rdata(memory_rdata),.memory_rvalid(memory_rvalid),.memory_fault(memory_fault),
        .fault(cache_fault),.misses(),.wait_cycles());
    // machine_reset asserts cartridge reset, which suppresses rom_stall.
    // Compute pause permission before the late ROM-stall gate, without latency.
    (* keep="true" *) wire chip_run_permitted=machine_reset || !(pause_request || save_pause);
    wire run_chip=!rom_stall && chip_run_permitted;
    // CE is sampled by the dedicated buffer while its input clock is low.
    // No phase counter, CPU edge, VDP DMA edge, or controller timer advances
    // while the external ROM service has not supplied the requested word.
    BUFGCE chip_clock_i(.I(core_clk),.CE(run_chip),.O(chip_clk));
    // Synchronous host-byte SRAM reads are assembled only after the chip has
    // stopped for the coherent snapshot. The loader owns this port in reset.
    reg [1:0] save_read_state;
    reg [15:0] save_read_base;
    reg [2:0] save_read_index;
    wire [15:0] host_save_addr=loader_save_write ? loader_save_addr : save_read_base+{13'd0,save_read_index};
    wire [7:0] host_save_q;
    assign save_memory_ready=save_read_state==0 && !loader_save_write && !load_valid;
    always @(posedge core_clk)begin
        save_memory_response<=0;
        if(reset)begin save_read_state<=0;save_read_base<=0;save_read_index<=0;save_memory_data<=0;end
        else case(save_read_state)
        0:if(save_memory_read && save_memory_ready)begin
            save_read_base<=save_memory_addr[15:0];save_read_index<=0;save_read_state<=1;
        end
        1:save_read_state<=2;
        2:begin
            save_memory_data[save_read_index*8+:8]<=host_save_q;
            if(save_read_index==7)begin save_read_state<=0;save_memory_response<=1;end
            else begin save_read_index<=save_read_index+1'b1;save_read_state<=1;end
        end
        default:save_read_state<=0;
        endcase
    end
    wire region_jp=region==MT_GENESIS_REGION_JP;
    multitini_genesis_machine machine_i(
        .clk(core_clk),.machine_clk(chip_clk),.reset(machine_reset),
        .pause_request(pause_request || save_pause),.region_jp(region_jp),.region_pal(PAL_MODE),
        .six_button(six_button),.fm_chip(fm_chip),.lowpass(lowpass),.buttons0(buttons0),.buttons1(buttons1),
        .cart_download(cart_download),.cart_load_wr(cart_word_write),.cart_load_addr(cart_word_address),.cart_load_data(cart_word_data),
        .save_addr(host_save_addr),.save_wr(loader_save_write),.save_data(loader_save_data),.save_q(host_save_q),.save_changed(save_changed),
        .rom_addr(rom_addr),.rom_request(rom_request),.rom_data_valid(rom_cached || rom_padding),
        .rom_data(rom_padding ? 16'hffff : rom_data),.rom_stall(rom_stall),.machine_ready(machine_ready),
        .video_r(video_r),.video_g(video_g),.video_b(video_b),.video_ce(chip_video_ce),
        .video_de(video_de),.video_vs(video_vsync),.video_240(video_mode_240),
        .audio_l(chip_audio_l),.audio_r(chip_audio_r));
    assign video_hsync=1'b0;
    // A held chip CE cannot be interpreted as repeated pixels by the ungated
    // video normalizer. Its samples occur only when this chip edge ran.
    assign video_valid=chip_video_ce && run_chip && ready;
    integer unsigned audio_phase;
    always @(posedge core_clk)begin
        audio_valid<=0;
        if(reset)begin audio_phase<=0;audio_l<=0;audio_r<=0;end
        else if(audio_phase>=CLOCK_HZ-AUDIO_SAMPLE_HZ)begin
            audio_phase<=audio_phase+AUDIO_SAMPLE_HZ-CLOCK_HZ;
            audio_valid<=1;
            audio_l<=ready && !pause_request && !save_pause ? {chip_audio_l,8'd0} : 24'd0;
            audio_r<=ready && !pause_request && !save_pause ? {chip_audio_r,8'd0} : 24'd0;
        end else audio_phase<=audio_phase+AUDIO_SAMPLE_HZ;
    end
    multitini_genesis_save #(.AUTO_FLUSH_CYCLES(CLOCK_HZ)) save_i (
        .clk(core_clk), .reset(reset), .clear_error(clear_load_error),
        .runtime_write_committed(save_changed),
        .runtime_write_address(24'd0),
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
        .media_state_mounted(media_state_mounted),
        .media_state_dirty(media_state_dirty),
        .media_state_eject_pending(media_state_eject_pending),
        .memory_read(save_memory_read), .memory_address(save_memory_addr),
        .memory_ready(save_memory_ready), .memory_rdata(save_memory_data),
        .memory_rvalid(save_memory_response), .pause_core(save_pause),
        .error(save_error)
    );
endmodule

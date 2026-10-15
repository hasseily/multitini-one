// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// NES/Famicom NTSC adapter to the shared Multitini transports.
module multitini_nes_adapter #(
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

    input logic load_abort,load_abort_epoch,load_epoch,
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

    output logic [7:0] video_x,video_y,
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
localparam [23:0] PALETTE[0:63] = '{
	'h666666, 'h01247B, 'h1B1489, 'h39087C, 'h520257, 'h5C0725, 'h571300, 'h472300,
	'h2D3300, 'h0E4000, 'h004500, 'h004124, 'h003456, 'h000000, 'h000000, 'h000000,
	'hADADAD, 'h2759C9, 'h4845DB, 'h6F34CA, 'h922B9B, 'hA1305A, 'h9B4018, 'h885400,
	'h686700, 'h3E7A00, 'h1B8213, 'h0D7C57, 'h136C99, 'h000000, 'h000000, 'h000000,
	'hFFFFFF, 'h78ABFF, 'h9897FF, 'hC086FF, 'hE27DEF, 'hF281AF, 'hED916D, 'hDBA43B,
	'hBDB825, 'h92CB33, 'h6DD463, 'h5ECEA8, 'h65BEEA, 'h525252, 'h000000, 'h000000,
	'hFFFFFF, 'hCADBFF, 'hD8D2FF, 'hE7CCFF, 'hF4C9F9, 'hFACBDF, 'hF7D2C4, 'hEEDAAF,
	'hE1E3A5, 'hD0EBAB, 'hC2EEBF, 'hBDEBDB, 'hC0E4F7, 'hB8B8B8, 'h000000, 'h000000
};

    import multitini_nes_defs_pkg::*;
    logic loader_error,save_error,load_active,invalidate;
    logic [63:0] mapper_flags;
    logic [9:0] prg_mask,chr_mask;
    logic [23:0] loader_addr;
    logic [63:0] loader_wdata;
    logic [7:0] loader_wstrb;
    logic loader_valid,loader_ready,loader_response;
    logic [12:0] save_address;
    logic [7:0] save_data;
    logic save_write,save_pause,save_read,save_ready,save_rvalid;
    logic [23:0] save_addr;
    logic [63:0] save_rdata;
    logic runtime_write_committed;
    logic [23:0] runtime_write_address;
    wire save_reset=reset||(invalidate&&!media_state_mounted&&
        !media_state_dirty&&!media_state_eject_pending);
    logic machine_reset,machine_ce,memory_stall;
    logic master_phase;
    logic [15:0] tick_debt;
    logic timing_fault;
    logic [24:0] cpu_addr;
    logic cpu_read,cpu_write;
    logic [7:0] cpu_rdata,cpu_wdata;
    logic [21:0] ppu_addr;
    logic ppu_read,ppu_write;
    logic [7:0] ppu_rdata,ppu_wdata;
    logic [15:0] raw_sample;
    logic [5:0] color;
    logic [2:0] emphasis,joy_out;
    logic [1:0] joy_clock,previous_joy_clock;
    logic [7:0] buttons0,buttons1,serial0,serial1;
    logic [8:0] cycle,scanline;
    logic pixel_ce,nes_hsync,nes_vsync,nes_hblank,nes_vblank;

    assign core_id=MT_NES_CORE_ID;
    assign load_error=loader_error||save_error||memory_fault||timing_fault;
    assign ready=!machine_reset&&!load_active;
    assign machine_reset=reset||core_reset_request||!cart_loaded||loader_error||memory_fault||timing_fault;
    // Only BUFG/MMCM clocks enter the machine. Every original master-edge
    // register advances together; PSRAM backpressure never gates a clock net.
    // One native master tick is due every two service cycles. A ROM miss
    // delays work; repay bounded debt with consecutive service CEs once data
    // arrives. CPU/PPU/APU and mapper time remain coherent, while the mean
    // frame cadence retains the exact native clock ratio. Menu and save
    // barriers deliberately stop time and accrue no debt.
    wire native_tick=master_phase==0;
    assign machine_ce=machine_reset?native_tick:
        (!pause_request&&!save_pause&&!memory_stall&&(native_tick||tick_debt!=0));
    always @(posedge core_clk)begin
        if(reset)begin master_phase<=0;tick_debt<=0;timing_fault<=0;end
        else begin
            master_phase<=master_phase+1'b1;
            if(clear_load_error)timing_fault<=0;
            if(machine_reset||pause_request||save_pause)tick_debt<=0;
            else case({native_tick,machine_ce})
                2'b10:if(tick_debt==16'hffff)timing_fault<=1;else tick_debt<=tick_debt+1'b1;
                2'b01:tick_debt<=tick_debt-1'b1;
                default:;
            endcase
        end
    end

    multitini_nes_loader loader(
        .clk(core_clk),.reset(reset),.clear_error(clear_load_error),
        .error(loader_error),.active(load_active),.invalidate(invalidate),
        .write_addr(loader_addr),.write_data(loader_wdata),.write_strobe(loader_wstrb),
        .write_valid(loader_valid),.write_ready(loader_ready),.write_response(loader_response),.*
    );
    multitini_nes_save save_service(
        .clk(core_clk),.reset(save_reset),.clear_error(clear_load_error),
        .memory_read(save_read),.memory_address(save_addr),.memory_ready(save_ready),
        .memory_rdata(save_rdata),.memory_rvalid(save_rvalid),.pause_core(save_pause),
        .error(save_error),.*
    );
    multitini_nes_memory memory_service(
        .clk(core_clk),.reset(reset),.master_ce(machine_ce),.chr_is_ram(mapper_flags[15]),
        .stall(memory_stall),.save_addr(save_addr),.loader_addr(loader_addr),
        .loader_wdata(loader_wdata),.loader_wstrb(loader_wstrb),.loader_valid(loader_valid),
        .loader_ready(loader_ready),.loader_response(loader_response),.*
    );

    assign input_ready=!reset;
    always @(posedge core_clk)begin
        if(reset)begin buttons0<=0;buttons1<=0;end
        else if(input_valid&&input_ready&&input_code[15:8]==8'h50)begin
            if(input_code[7:0]==0)buttons0<=input_value[7:0];
            if(input_code[7:0]==1)buttons1<=input_value[7:0];
        end
        if(machine_reset)begin serial0<=8'h00;serial1<=8'h00;previous_joy_clock<=0;end
        else if(machine_ce)begin
            previous_joy_clock<=joy_clock;
            // NES serial order: A,B,Select,Start,Up,Down,Left,Right.
            if(joy_out[0])begin
                serial0<={buttons0[0],buttons0[1],buttons0[2],buttons0[3],buttons0[7],buttons0[6],buttons0[5],buttons0[4]};
                serial1<={buttons1[0],buttons1[1],buttons1[2],buttons1[3],buttons1[7],buttons1[6],buttons1[5],buttons1[4]};
            end else begin
                if(!joy_clock[0]&&previous_joy_clock[0])serial0<={1'b1,serial0[7:1]};
                if(!joy_clock[1]&&previous_joy_clock[1])serial1<={1'b1,serial1[7:1]};
            end
        end
    end
    NES machine(
        .clk(core_clk),.ce_master(machine_ce),.reset_nes(machine_reset),
        .cold_reset(machine_reset),.ppu_rst_behavior(1'b1),.pausecore(1'b0),
        .sys_type(2'd0),.vs_dip_switches(8'd0),.mapper_flags(mapper_flags),
        .sample(raw_sample),.color(color),.emphasis(emphasis),.pixel_ce(pixel_ce),
        .joypad_clock(joy_clock),.joypad_out(joy_out),
        .joypad1_data({4'd0,serial0[0]}),.joypad2_data({4'd0,serial1[0]}),
        .fds_busy(1'b0),.fds_eject(1'b0),.fds_auto_eject(1'b0),.max_diskside(2'd0),.fds_fast(1'b0),
        .audio_channels(5'b11111),.ex_sprites(1'b0),.mask(2'd0),.dejitter_timing(1'b1),
        .cpumem_addr(cpu_addr),.cpumem_read(cpu_read),.cpumem_write(cpu_write),
        .cpumem_dout(cpu_wdata),.cpumem_din(cpu_rdata),
        .ppumem_addr(ppu_addr),.ppumem_read(ppu_read),.ppumem_write(ppu_write),
        .ppumem_dout(ppu_wdata),.ppumem_din(ppu_rdata),
        .prg_mask(prg_mask),.chr_mask(chr_mask),.bram_din(8'hff),
        .cycle(cycle),.scanline(scanline),.int_audio(1'b1),.ext_audio(1'b0),
        .gg(1'b0),.gg_code(129'd0),.gg_reset(machine_reset),.debug_dots(1'b0),
        .increaseSSHeaderCount(1'b0),.save_state(1'b0),.load_state(1'b0),.savestate_number(2'd0),
        .hsync(nes_hsync),.hblank(nes_hblank),.vsync(nes_vsync),.vblank(nes_vblank),
        .Savestate_SDRAMReadData(8'd0),.SaveStateExt_Dout(64'd0),.SAVE_out_Dout(64'd0),.SAVE_out_done(1'b0)
    );

    multitini_nes_audio #(.CORE_HZ(MT_NES_CORE_HZ),.AUDIO_SAMPLE_HZ(AUDIO_SAMPLE_HZ)) audio_service(
        .clk(core_clk),.reset(reset),.machine_reset(machine_reset),
        .mute(pause_request||save_pause),.sample(raw_sample),
        .audio_l(audio_l),.audio_r(audio_r),.audio_valid(audio_valid)
    );

    // Upstream PPU's registered color represents the previous dot. Original
    // video.sv exposes dots1..256; preserve that alignment and all240rows.
    wire [23:0] palette_color=PALETTE[color];
    wire [7:0] red=palette_color[23:16],green=palette_color[15:8],blue=palette_color[7:0];
    function automatic [7:0] tint(input [7:0] component,input attenuate);
        tint=attenuate?component-(component>>2):component;
    endfunction
    always @(posedge core_clk)begin
        video_valid<=0;
        if(reset)begin
            video_r<=0;video_g<=0;video_b<=0;video_de<=0;video_x<=0;video_y<=0;
            video_hsync<=0;video_vsync<=0;
        end else begin
            if(machine_ce&&pixel_ce)begin
                video_x<=cycle[7:0]-1'b1;video_y<=scanline[7:0];
                video_de<=!machine_reset&&cycle>=1&&cycle<=256&&scanline<240;
                video_valid<=!machine_reset;
                video_r<=tint(red,emphasis[1]||emphasis[2]);
                video_g<=tint(green,emphasis[0]||emphasis[2]);
                video_b<=tint(blue,emphasis[0]||emphasis[1]);
                video_hsync<=nes_hsync;video_vsync<=nes_vsync;
            end
        end
    end
endmodule

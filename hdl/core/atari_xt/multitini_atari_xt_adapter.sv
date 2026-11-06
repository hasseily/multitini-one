// SPDX-License-Identifier: GPL-3.0-only
// Integrates the Atari-XT machine logic by ThrudTheBarbarian.
// Source Location: https://github.com/ThrudTheBarbarian/fpga-xt
`timescale 1ns/1ps

module multitini_atari_xt_adapter #(
    parameter string PALETTE_FILE = "atari_ntsc.hex"
) (
    input  wire              core_clk, reset, core_reset_request, pause_request, clear_load_error,
    input  wire [15:0]       input_code, input_value,
    input  wire              input_valid,
    output wire              input_ready,
    input  wire [7:0]        load_data, load_kind,
    input  wire              load_valid, load_first, load_last, load_epoch, load_abort, load_abort_epoch,
    input  wire [31:0]       load_offset, load_total,
    output wire              load_ready, ready,
    output reg               load_error,
    output wire [2:0]        rom_loaded,
    output wire              sio_reset, sio_event_valid,
    output wire [17:0]       sio_event_data,
    input  wire              sio_event_ready,
    input  wire              sio_reply_valid,
    input  wire [16:0]       sio_reply_data,
    output wire              sio_reply_ready,
    output wire [7:0]        video_r, video_g, video_b,
    output reg  [11:0]       video_x, video_y,
    output reg               video_valid,
    output reg signed [23:0] audio_l, audio_r,
    output reg               audio_valid
);
    localparam [7:0] OS_KIND = 8'h78;
    localparam [7:0] BASIC_KIND = 8'h79;
    localparam [7:0] CART_KIND = 8'h7a;
    localparam [7:0] EJECT_KIND = 8'h7b;

    reg os_valid, basic_valid, cart_valid, cart_16k, cart_blocked;
    reg transfer_active, transfer_epoch, aborted_valid, aborted_epoch;
    reg [7:0] transfer_kind;
    reg [14:0] transfer_total, expected_offset;

    wire accepted_kind = (load_kind == OS_KIND && load_total == 16384) ||
        (load_kind == BASIC_KIND && load_total == 8192) ||
        (load_kind == CART_KIND && (load_total == 8192 || load_total == 16384)) ||
        (load_kind == EJECT_KIND && load_total == 1 && load_data == 0);
    wire first_ok = load_offset == 0 && !transfer_active && accepted_kind;
    wire continuation_ok = transfer_active && load_epoch == transfer_epoch && load_kind == transfer_kind &&
        load_total == {17'd0, transfer_total} && load_offset == {17'd0, expected_offset};
    wire sequence_ok = core_reset_request && (load_first ? first_ok : continuation_ok) &&
        load_offset < load_total && load_last == (load_offset + 1'b1 == load_total);
    wire stale = (aborted_valid && load_epoch == aborted_epoch) || (load_abort && load_epoch == load_abort_epoch);
    wire take = load_valid && load_ready && !stale;
    wire rom_write = take && sequence_ok && load_kind != EJECT_KIND;
    wire machine_reset = reset || core_reset_request || !os_valid || !basic_valid || cart_blocked || transfer_active;

    assign load_ready = !reset;
    assign ready = !machine_reset;
    assign input_ready = !reset;
    assign rom_loaded = {basic_valid, cart_valid, os_valid};

    always @(posedge core_clk) begin
        if (reset) begin
            os_valid <= 0;
            basic_valid <= 0;
            cart_valid <= 0;
            cart_16k <= 0;
            cart_blocked <= 0;
            transfer_active <= 0;
            aborted_valid <= 0;
            load_error <= 0;
        end else begin
            if (clear_load_error) load_error <= 0;
            if (load_abort) begin
                aborted_valid <= 1;
                aborted_epoch <= load_abort_epoch;
                if (transfer_active && transfer_epoch == load_abort_epoch) begin
                    transfer_active <= 0;
                    load_error <= 1;
                end
            end
            if (take) begin
                if (!sequence_ok) begin
                    load_error <= 1;
                    transfer_active <= 0;
                    if (load_kind == CART_KIND || load_kind == EJECT_KIND) begin
                        cart_valid <= 0;
                        cart_blocked <= 1;
                    end
                end else begin
                    if (load_first) begin
                        aborted_valid <= 0;
                        transfer_epoch <= load_epoch;
                        transfer_kind <= load_kind;
                        transfer_total <= load_total[14:0];
                        if (load_kind == OS_KIND) os_valid <= 0;
                        if (load_kind == BASIC_KIND) basic_valid <= 0;
                        if (load_kind == CART_KIND) begin
                            cart_valid <= 0;
                            cart_blocked <= 1;
                        end
                    end
                    transfer_active <= !load_last;
                    expected_offset <= load_offset[14:0] + 1'b1;
                    if (load_last) begin
                        case (load_kind)
                            OS_KIND: os_valid <= 1;
                            BASIC_KIND: basic_valid <= 1;
                            CART_KIND: begin
                                cart_valid <= 1;
                                cart_16k <= load_total == 16384;
                                cart_blocked <= 0;
                            end
                            EJECT_KIND: begin
                                cart_valid <= 0;
                                cart_blocked <= 0;
                            end
                            default: ;
                        endcase
                    end
                end
            end
        end
    end

    // 56 fabric clocks per phi2 and four high-resolution pixels per phi2,
    // matching the upstream fidelity CPU path at its 100 MHz operating point.
    reg [5:0] phase;
    always @(posedge core_clk) begin
        if (machine_reset || phase == 55) phase <= 0;
        else phase <= phase + 1'b1;
    end
    wire tick = phase == 55;
    wire pixel_tick = tick || phase == 13 || phase == 27 || phase == 41;

    wire [15:0] cpu_addr, antic_addr;
    wire [7:0] cpu_out;
    wire cpu_write, cpu_rom_hit, antic_rom_hit;
    wire [14:0] cpu_rom_addr, antic_rom_addr;
    wire [7:0] consol, porta, key_code;
    wire key_make, key_release, key_break;
    wire [1:0] trigger_n;
    wire [3:0] channel1, channel2, channel3, channel4;
    wire pixel_write, line_start;
    wire [7:0] color_code;
    wire [8:0] line;
    wire command_n, serial_tx_valid, serial_rx_mode, serial_rx_read;
    wire serial_rx_valid, serial_rx_busy, serial_rx_overrun;
    wire [7:0] serial_tx_data, serial_rx_data;

    assign sio_reset = machine_reset;
    multitini_atari_xt_sio sio (
        .clk(core_clk), .reset(reset), .machine_reset(machine_reset), .paused(pause_request),
        .command_n(command_n), .tx_valid(serial_tx_valid), .tx_data(serial_tx_data),
        .rx_mode(serial_rx_mode), .rx_read(serial_rx_read), .rx_valid(serial_rx_valid),
        .rx_busy(serial_rx_busy), .rx_overrun(serial_rx_overrun), .rx_data(serial_rx_data),
        .event_valid(sio_event_valid), .event_data(sio_event_data), .event_ready(sio_event_ready),
        .reply_valid(sio_reply_valid), .reply_data(sio_reply_data), .reply_ready(sio_reply_ready)
    );

    multitini_atari_xt_keyboard keyboard (
        .clk(core_clk), .reset(machine_reset),
        .input_code(input_code), .input_value(input_value), .input_valid(input_valid),
        .key_make(key_make), .key_release(key_release), .key_break(key_break), .key_code(key_code),
        .consol(consol), .porta(porta), .trigger_n(trigger_n)
    );

    // CPU and ANTIC each see a one-clock read port. Keep the RAM underneath
    // ROM and cartridge apertures untouched while those windows are enabled.
    (* ram_style = "block" *) reg [7:0] ram [0:65535];
    // Each ROM copy has a dedicated loader write port. A shared CPU read and
    // loader write address was optimized to the CPU address in the routed design.
    (* ram_style = "block" *) reg [7:0] cpu_rom [0:40959];
    (* ram_style = "block" *) reg [7:0] antic_rom [0:40959];
    reg [7:0] cpu_ram_q, antic_ram_q, cpu_rom_q, antic_rom_q;
    reg cpu_rom_select, antic_rom_select, cpu_open_bus;
    wire cpu_cart_hit = cart_valid && (cpu_addr[15:13] == 3'b101 ||
        (cart_16k && cpu_addr[15:13] == 3'b100));
    wire antic_cart_hit = cart_valid && (antic_addr[15:13] == 3'b101 ||
        (cart_16k && antic_addr[15:13] == 3'b100));
    wire cpu_io = cpu_addr[15:11] == 5'b11010;
    wire [15:0] loader_base = load_kind == OS_KIND ? 16'h0000 :
        load_kind == BASIC_KIND ? 16'h4000 : 16'h6000;
    wire [15:0] rom_load_addr = loader_base + load_offset[15:0];
    wire [15:0] cpu_cart_addr = 16'h6000 + {2'b00, cart_16k && cpu_addr[13], cpu_addr[12:0]};
    wire [15:0] antic_cart_addr = 16'h6000 + {2'b00, cart_16k && antic_addr[13], antic_addr[12:0]};
    wire [15:0] cpu_rom_read_addr = cpu_cart_hit ? cpu_cart_addr : {1'b0, cpu_rom_addr};
    wire [15:0] antic_rom_read_addr = antic_cart_hit ? antic_cart_addr : {1'b0, antic_rom_addr};

    always @(posedge core_clk) begin
        if (rom_write) begin
            cpu_rom[rom_load_addr] <= load_data;
            antic_rom[rom_load_addr] <= load_data;
        end
        cpu_rom_q <= cpu_rom[cpu_rom_read_addr];
        antic_rom_q <= antic_rom[antic_rom_read_addr];
        cpu_rom_select <= cpu_rom_hit || cpu_cart_hit;
        antic_rom_select <= antic_rom_hit || antic_cart_hit;
        cpu_open_bus <= cpu_io;
        if (cpu_write && ready && !cpu_rom_hit && !cpu_cart_hit && !cpu_io) ram[cpu_addr] <= cpu_out;
        cpu_ram_q <= ram[cpu_addr];
        antic_ram_q <= ram[antic_addr];
    end

    multitini_atari_xt_machine #(.USE_ANTIC2(1'b1)) machine (
        .clk(core_clk), .rst(machine_reset), .cold(machine_reset), .hold_cpu(pause_request),
        .tick(tick), .px_tick(pixel_tick), .tune(16'd0),
        .cpu_addr(cpu_addr), .cpu_wdata(cpu_out), .cpu_we(cpu_write),
        .cpu_rdata(cpu_rom_select ? cpu_rom_q : cpu_open_bus ? 8'hff : cpu_ram_q),
        .rom_hit(cpu_rom_hit), .rom_addr(cpu_rom_addr),
        .antic_addr(antic_addr), .antic_rdata(antic_rom_select ? antic_rom_q : antic_ram_q),
        .antic_rom_hit(antic_rom_hit), .antic_rom_addr(antic_rom_addr),
        .irq_n(1'b1), .trig0({7'd0, trigger_n[0]}), .trig1({7'd0, trigger_n[1]}),
        // XL/XE TRIG3 reads RD5 directly. RD5 is high when a cartridge
        // supplies the upper cartridge window and low for built-in BASIC.
        .trig2(8'h01), .trig3({7'd0, cart_valid}), .pal_sense(8'h0f),
        .consol_keys(consol), .joy_porta(porta),
        .kbd_event_valid(key_make), .kbd_release(key_release), .break_key_pulse(key_break), .kbd_event_code(key_code),
        .audio_ch1(channel1), .audio_ch2(channel2), .audio_ch3(channel3), .audio_ch4(channel4),
        .sio_command_n(command_n), .sio_tx_valid(serial_tx_valid), .sio_tx_data(serial_tx_data),
        .sio_rx_mode(serial_rx_mode), .sio_rx_read(serial_rx_read), .sio_rx_valid(serial_rx_valid),
        .sio_rx_busy(serial_rx_busy), .sio_rx_overrun(serial_rx_overrun), .sio_rx_data(serial_rx_data),
        .lb_wr(pixel_write), .lb_color(color_code), .lb_line_start(line_start),
        .dma_steal(), .rdy_n(), .nmi_n(), .sync(), .dbg_pc(), .dbg_a(), .dbg_x(),
        .dbg_y(), .dbg_s(), .dbg_p(), .hcount(), .line(line)
    );

    // Atari-XT's line marker is delayed alongside its color pipeline.
    // Count its output samples, with the same origin as antic_wb_adapt.
    // Extending its 320-pixel crop by 32 on each side keeps wide playfields.
    reg [8:0] source_x, source_y;
    palette_lut #(.INIT_FILE(PALETTE_FILE)) palette (
        .clk(core_clk), .we(1'b0), .waddr(8'd0), .wdata(24'd0),
        .raddr(color_code), .rdata({video_r, video_g, video_b})
    );
    always @(posedge core_clk) begin
        video_valid <= 0;
        if (machine_reset) begin
            source_x <= 0;
            source_y <= 0;
            video_x <= 0;
            video_y <= 0;
        end else if (line_start) begin
            source_x <= 0;
            source_y <= line;
        end else if (pixel_write) begin
            source_x <= source_x + 1'b1;
            if (source_x >= 64 && source_x < 448 && source_y >= 8 && source_y < 248) begin
                video_x <= {3'd0, source_x} - 12'd64;
                video_y <= {3'd0, source_y} - 12'd8;
                video_valid <= 1;
            end
        end
    end

    // POKEY channel levels are unipolar. Remove their slowly varying DC
    // offset and send one signed sample to both of the shared DAC channels.
    wire [5:0] mixed = {2'd0, channel1} + {2'd0, channel2} + {2'd0, channel3} + {2'd0, channel4};
    wire signed [23:0] sample_q10 = $signed({8'd0, mixed, 10'd0});
    reg signed [23:0] dc_offset;
    wire signed [23:0] audio_ac = sample_q10 - dc_offset;
    reg [11:0] audio_div;
    always @(posedge core_clk) begin
        audio_valid <= 0;
        if (reset) begin
            audio_div <= 0;
            dc_offset <= 0;
            audio_l <= 0;
            audio_r <= 0;
        end else if (audio_div == 12'd2082) begin
            audio_div <= 0;
            audio_valid <= 1;
            dc_offset <= machine_reset || pause_request ? 24'sd0 : dc_offset + (audio_ac >>> 9);
            audio_l <= machine_reset || pause_request ? 24'sd0 : audio_ac <<< 7;
            audio_r <= machine_reset || pause_request ? 24'sd0 : audio_ac <<< 7;
        end else audio_div <= audio_div + 1'b1;
    end
endmodule

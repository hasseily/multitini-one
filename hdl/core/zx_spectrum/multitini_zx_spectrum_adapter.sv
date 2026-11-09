// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps

module multitini_zx_spectrum_adapter (
    input  wire               core_clk, reset, core_reset_request, pause_request, clear_load_error,
    input  wire [1:0]         tape_speed,
    input  wire               stock_tape_rom,
    input  wire [15:0]        input_code, input_value,
    input  wire               input_valid,
    output wire               input_ready,
    input  wire [7:0]         load_data, load_kind,
    input  wire               load_valid, load_first, load_last, load_epoch, load_abort, load_abort_epoch,
    input  wire [31:0]        load_offset, load_total,
    output wire               load_ready, ready,
    output reg                load_error,
    output wire [2:0]         rom_loaded,
    output wire [7:0]         video_r, video_g, video_b,
    output wire [11:0]        video_x, video_y,
    output wire               video_valid,
    output reg signed [23:0]  audio_l, audio_r,
    output reg                audio_valid
);
    localparam [7:0] ROM_KIND = 8'h70;
    localparam [7:0] TAP_KIND = 8'h71;
    localparam [7:0] TZX_KIND = 8'h72;
    localparam [31:0] MAX_TAP_BYTES = 131072;
    localparam [1:0] TAPE_FAST = 2'd1, TAPE_INSTANT = 2'd2;

    reg rom_valid, mode_128, tape_loaded, tape_tzx;
    reg transfer_active, transfer_epoch, aborted_valid, aborted_epoch;
    reg [7:0] transfer_kind;
    reg [17:0] expected_offset, transfer_total, tape_size, tape_block_end;
    reg [7:0] tape_len_lo;
    typedef enum logic [1:0] {TAP_LEN_LO, TAP_LEN_HI, TAP_DATA} tap_validator_t;
    tap_validator_t tape_validate;

    wire [17:0] next_offset = load_offset[17:0] + 1'b1;
    wire [17:0] next_block_end = next_offset + {2'b0, load_data, tape_len_lo};
    wire tape_kind = load_kind == TAP_KIND || load_kind == TZX_KIND;
    wire first_ok = load_offset == 0 && (!transfer_active || load_epoch != transfer_epoch) &&
        ((load_kind == ROM_KIND && (load_total == 16384 || load_total == 32768)) ||
         (load_kind == TAP_KIND && load_total >= 3 && load_total <= MAX_TAP_BYTES) ||
         (load_kind == TZX_KIND && load_total != 0 && load_total <= MAX_TAP_BYTES));
    wire continuation_ok = transfer_active && load_epoch == transfer_epoch && load_kind == transfer_kind &&
        load_total == {14'b0, transfer_total} && load_offset == {14'b0, expected_offset};
    wire sequence_ok = (load_first ? first_ok : continuation_ok) &&
        load_offset < load_total && load_last == (load_offset + 1'b1 == load_total);
    wire stale = (aborted_valid && load_epoch == aborted_epoch) || (load_abort && load_epoch == load_abort_epoch);
    wire take = load_valid && load_ready && !stale;
    wire tape_valid = load_kind != TAP_KIND || load_first ||
        ((tape_validate != TAP_LEN_HI || ({load_data, tape_len_lo} != 0 && next_block_end <= load_total)) &&
         (!load_last || (tape_validate == TAP_DATA && next_offset == tape_block_end)));
    wire rom_write = take && sequence_ok && load_kind == ROM_KIND;
    wire tape_write = take && sequence_ok && tape_kind && tape_valid;
    wire machine_reset = reset || core_reset_request || !rom_valid || (rom_write && load_first);

    assign load_ready = !reset;
    assign ready = !machine_reset;
    assign rom_loaded = {1'b0, tape_loaded, rom_valid};

    always @(posedge core_clk) begin
        if (reset) begin
            rom_valid <= 0;
            mode_128 <= 0;
            tape_loaded <= 0;
            tape_tzx <= 0;
            tape_size <= 0;
            transfer_active <= 0;
            aborted_valid <= 0;
            load_error <= 0;
        end else begin
            if (clear_load_error) load_error <= 0;
            if (load_abort) begin
                aborted_valid <= 1;
                aborted_epoch <= load_abort_epoch;
                // The epoch is shared across successful streams. An abort
                // before the first byte cannot cancel a previous asset.
                if (transfer_active && transfer_epoch == load_abort_epoch) transfer_active <= 0;
            end
            if (take) begin
                if (!sequence_ok || !tape_valid) begin
                    load_error <= 1;
                    transfer_active <= 0;
                end else begin
                    if (load_first) begin
                        aborted_valid <= 0;
                        transfer_epoch <= load_epoch;
                        transfer_kind <= load_kind;
                        transfer_total <= load_total[17:0];
                        if (load_kind == ROM_KIND) rom_valid <= 0;
                        if (tape_kind) tape_loaded <= 0;
                    end
                    transfer_active <= !load_last;
                    expected_offset <= next_offset;
                    if (load_kind == ROM_KIND && load_last) begin
                        mode_128 <= load_total == 32768;
                        rom_valid <= 1;
                    end
                    if (load_kind == TAP_KIND) begin
                        if (load_first || tape_validate == TAP_LEN_LO) begin
                            tape_len_lo <= load_data;
                            tape_validate <= TAP_LEN_HI;
                        end else if (tape_validate == TAP_LEN_HI) begin
                            tape_block_end <= next_block_end;
                            tape_validate <= TAP_DATA;
                        end else if (next_offset == tape_block_end) tape_validate <= TAP_LEN_LO;
                    end
                    if (tape_kind && load_last) begin
                        tape_loaded <= 1;
                        tape_tzx <= load_kind == TZX_KIND;
                        tape_size <= load_total[17:0];
                    end
                end
            end
        end
    end

    reg [4:0] phase;
    always @(posedge core_clk) begin
        if (reset) phase <= 0;
        else phase <= phase + 1'b1;
    end
    wire ce_7mp = phase[2:0] == 0;
    wire ce_7mn = phase[2:0] == 4;
    wire tape_playing, tape_underflow;
    wire ce_cpu_p, ce_cpu_n;
    reg fast_tape_active, cpu_phase_high, tape_stalled;
    wire fast_tape_requested = tape_speed == TAPE_FAST && tape_playing;
    wire switch_tape_speed = !cpu_phase_high && fast_tape_active != fast_tape_requested;
    // T80pa consumes alternating positive and negative enables. Switch only
    // between a completed negative phase and the next positive phase.
    wire tape_tick_due = !switch_tape_speed && (fast_tape_active ? !phase[0] : phase[3:0] == 0);
    wire tape_tick = tape_tick_due && !tape_underflow;
    // At stock speed the ULA can start a CPU phase between tape ticks. Only
    // hold the CPU after a tape tick actually misses its next TZX event.
    // In Fast mode the CPU and tape can restart together on a due tick.
    wire hold_tape_cpu = tape_tick_due ? tape_underflow : tape_stalled;
    wire cpu_p = !pause_request && !switch_tape_speed && !hold_tape_cpu &&
        (fast_tape_active ? !phase[0] : ce_cpu_p);
    // Complete any accepted positive phase even when the tape is stalled.
    wire cpu_n = !pause_request && (fast_tape_active ? phase[0] : ce_cpu_n);
    always @(posedge core_clk) begin
        if (machine_reset || !tape_playing) tape_stalled <= 0;
        else if (!pause_request && tape_tick_due) tape_stalled <= tape_underflow;
        if (machine_reset) begin
            fast_tape_active <= 0;
            cpu_phase_high <= 0;
        end else begin
            if (switch_tape_speed) fast_tape_active <= fast_tape_requested;
            if (cpu_p && !cpu_phase_high) cpu_phase_high <= 1;
            else if (cpu_n && cpu_phase_high) cpu_phase_high <= 0;
        end
    end
    wire nM1, nMREQ, nIORQ, nRD, nWR, nRFSH, nINT;
    wire [15:0] cpu_addr;
    wire [7:0] cpu_out;
    wire [7:0] cpu_in;
    T80pa cpu (
        .RESET_n(!machine_reset), .CLK(core_clk),
        .CEN_p(cpu_p), .CEN_n(cpu_n),
        .WAIT_n(1'b1), .INT_n(nINT), .NMI_n(1'b1), .BUSRQ_n(1'b1),
        .M1_n(nM1), .MREQ_n(nMREQ), .IORQ_n(nIORQ), .RD_n(nRD), .WR_n(nWR), .RFSH_n(nRFSH),
        .HALT_n(), .BUSAK_n(), .OUT0(1'b0), .A(cpu_addr), .DI(cpu_in), .DO(cpu_out),
        .R800_mode(1'b0), .REG(), .DIRSet(1'b0), .DIR(212'd0)
    );

    reg [5:0] paging;
    reg [2:0] border;
    reg speaker, microphone, previous_io_write;
    wire io_write = !nIORQ && !nWR && nM1;
    wire io_write_pulse = io_write && !previous_io_write && !machine_reset;
    wire io_read = !nIORQ && !nRD && nM1;
    wire ay_select = mode_128 && cpu_addr[15] && !cpu_addr[1];
    always @(posedge core_clk) begin
        previous_io_write <= io_write;
        if (machine_reset) begin
            paging <= 0;
            border <= 0;
            speaker <= 0;
            microphone <= 0;
        end else if (io_write_pulse) begin
            if (mode_128 && !paging[5] && !cpu_addr[15] && !cpu_addr[1]) paging <= cpu_out[5:0];
            if (!cpu_addr[0]) begin
                border <= cpu_out[2:0];
                microphone <= cpu_out[3];
                speaker <= cpu_out[4];
            end
        end
    end

    (* ram_style = "block" *) reg [7:0] rom [0:32767];
    (* ram_style = "block" *) reg [7:0] ram [0:131071];
    reg [7:0] rom_data, ram_data, vram_data;
    wire [14:0] vram_addr;
    wire [2:0] cpu_bank = cpu_addr[15] ? (cpu_addr[14] ? paging[2:0] : 3'd2) : 3'd5;
    wire [16:0] ram_addr = {cpu_bank, cpu_addr[13:0]};
    wire [16:0] screen_addr = {1'b1, vram_addr[14], 1'b1, vram_addr[13:0]};
    // Separate loader and CPU addresses keep writes independent of the held Z80.
    always @(posedge core_clk) begin
        if (rom_write) rom[load_offset[14:0]] <= load_data;
    end
    always @(posedge core_clk) begin
        if (!rom_write) rom_data <= rom[{paging[4], cpu_addr[13:0]}];
        if (!machine_reset && !nMREQ && !nWR && |cpu_addr[15:14]) ram[ram_addr] <= cpu_out;
        ram_data <= ram[ram_addr];
        vram_data <= ram[screen_addr];
    end

    wire [4:0] key_columns, joystick;
    wire tape_toggle, tape_rewind, tape_ear;
    wire instant_available, instant_byte_valid;
    wire [7:0] instant_byte;
    wire stock_rom_bank = stock_tape_rom && (!mode_128 || paging[4]);
    wire opcode_fetch = !nM1 && !nMREQ && !nRD;
    reg previous_opcode_fetch;
    wire opcode_start = opcode_fetch && !previous_opcode_fetch;
    reg instant_active, instant_byte_latched_valid;
    reg [7:0] instant_byte_latched;
    // LD-BYTES has already disabled interrupts and stacked the ROM's return
    // routine when its first EAR read is fetched. The rest of its byte loop
    // stays in this address range after we bypass the pulse decoder.
    wire instant_start = opcode_start && cpu_addr == 16'h0562 && !instant_active &&
        stock_rom_bank && tape_speed == TAPE_INSTANT && instant_available;
    wire instant_finish = opcode_start && instant_active &&
        (cpu_addr < 16'h0562 || cpu_addr > 16'h05e2);
    wire instant_next = opcode_start && instant_active && cpu_addr == 16'h05cc;
    always @(posedge core_clk) begin
        if (machine_reset) begin
            previous_opcode_fetch <= 0;
            instant_active <= 0;
            instant_byte_latched_valid <= 0;
            instant_byte_latched <= 0;
        end else begin
            previous_opcode_fetch <= opcode_fetch;
            if (instant_start) instant_active <= 1;
            if (instant_finish) instant_active <= 0;
            if (instant_active && opcode_start && cpu_addr == 16'h05ca) begin
                instant_byte_latched_valid <= instant_byte_valid;
                instant_byte_latched <= instant_byte;
            end
        end
    end

    // Overlay only two small sections in the stock loader. The ROM still
    // handles the flag, LOAD/VERIFY, addresses, length, and block parity.
    reg [7:0] tape_rom_data;
    always @* begin
        tape_rom_data = rom_data;
        if (instant_active || instant_start) begin
            case (cpu_addr)
                16'h0562: tape_rom_data = 8'h0e; // LD C,2 (EAR/border state)
                16'h0563: tape_rom_data = 8'h02;
                16'h0564: tape_rom_data = 8'hc3; // JP 059F (start of byte loading)
                16'h0565: tape_rom_data = 8'h9f;
                16'h0566: tape_rom_data = 8'h05;
                16'h05ca: tape_rom_data = (previous_opcode_fetch ? instant_byte_latched_valid : instant_byte_valid) ?
                    8'h2e : 8'ha7; // LD L,byte or AND A (clear carry for short block)
                16'h05cb: tape_rom_data = instant_byte_latched_valid ? instant_byte_latched : 8'hc9; // RET on error
                16'h05cc: tape_rom_data = 8'hc3; // JP 05D8 (stock parity update)
                16'h05cd: tape_rom_data = 8'hd8;
                16'h05ce: tape_rom_data = 8'h05;
                default: ;
            endcase
        end
    end

    assign input_ready = !reset;
    multitini_zx_spectrum_keyboard keyboard (
        .clk(core_clk), .reset(machine_reset), .input_code(input_code), .input_value(input_value),
        .input_valid(input_valid), .row_select(cpu_addr[15:8]),
        .columns(key_columns), .kempston(joystick), .tape_toggle(tape_toggle), .tape_rewind(tape_rewind)
    );
    multitini_zx_spectrum_tape tape (
        .clk(core_clk), .reset(machine_reset || (tape_write && load_first)),
        .tick(tape_tick), .instant_tick(!phase[0]), .paused(pause_request),
        .toggle(tape_toggle), .rewind(tape_rewind), .loaded(tape_loaded), .size(tape_size),
        .tzx(tape_tzx), .mode_128(mode_128),
        .write_enable(tape_write), .write_address(load_offset[16:0]), .write_data(load_data),
        .instant_start(instant_start), .instant_next(instant_next), .instant_finish(instant_finish),
        .instant_available(instant_available), .instant_byte_valid(instant_byte_valid), .instant_byte(instant_byte),
        .ear(tape_ear), .playing(tape_playing), .underflow(tape_underflow)
    );

    wire [7:0] floating_bus, ay_data;
    wire ay_read = ay_select && cpu_addr[14];
    assign cpu_in = !nMREQ ? (|cpu_addr[15:14] ? ram_data : tape_rom_data) :
        !io_read ? floating_bus : cpu_addr[5:0] == 6'h1f ? {3'b0, joystick} :
        ay_read ? ay_data : !cpu_addr[0] ? {1'b1, tape_ear, 1'b1, key_columns} : floating_bus;

    wire [8:0] raster_h, raster_v;
    wire bright, red, green, blue;
    multitini_zx_spectrum_ula ula (
        .reset(machine_reset), .clk_sys(core_clk), .ce_7mp(ce_7mp), .ce_7mn(ce_7mn),
        .ce_cpu_sp(ce_cpu_p), .ce_cpu_sn(ce_cpu_n), .addr(cpu_addr), .din(cpu_out),
        .nMREQ(nMREQ), .nIORQ(nIORQ), .nRFSH(nRFSH), .nRD(nRD), .nWR(nWR), .nINT(nINT),
        .vram_addr(vram_addr), .vram_dout(vram_data), .port_ff(floating_bus),
        .ulap_avail(1'b0), .ulap_sel(), .ulap_dout(), .ulap_ena(), .ulap_mono(), .ulap_color(),
        .tmx_avail(1'b0), .mode512(), .snow_ena(!mode_128 && !fast_tape_active),
        .mZX(1'b1), .m128(mode_128), .scorp_tim(1'b0),
        .page_scr(paging[3]), .page_ram(paging[2:0]), .border_color(border), .wide(2'b0), .overscan(2'b0),
        .raster_h(raster_h), .raster_v(raster_v), .HSync(), .VSync(), .HBlank(), .VBlank(),
        .I(bright), .R(red), .G(green), .B(blue)
    );

    // Both timings put the paper at raw H 12..267 and V 0..191. The
    // visible line begins 32 pixels before the ULA horizontal counter wraps.
    wire [8:0] left_edge = mode_128 ? 9'd424 : 9'd416;
    wire [8:0] top_edge = mode_128 ? 9'd267 : 9'd268;
    wire [8:0] last_line = mode_128 ? 9'd310 : 9'd311;
    wire left_border = raster_h >= left_edge;
    wire [8:0] native_y = left_border ? (raster_v == last_line ? 9'd0 : raster_v + 1'b1) : raster_v;
    assign video_x = left_border ? {3'b0, raster_h} - {3'b0, left_edge} : {3'b0, raster_h} + 12'd32;
    assign video_y = native_y >= top_edge ? {3'b0, native_y} - {3'b0, top_edge} : {3'b0, native_y} + 12'd44;
    assign video_valid = ce_7mp && (left_border || raster_h < 312) && video_y < 280 && ready;
    assign video_r = {red, red, {6{red && bright}}};
    assign video_g = {green, green, {6{green && bright}}};
    assign video_b = {blue, blue, {6{blue && bright}}};

    wire [7:0] ay_a, ay_b, ay_c;
    YM2149 ay (
        .CLK(core_clk), .CE(phase == 0 && !pause_request), .RESET(machine_reset),
        .BDIR(ay_select && io_write_pulse), .BC(cpu_addr[14]), .DI(cpu_out), .DO(ay_data),
        .CHANNEL_A(ay_a), .CHANNEL_B(ay_b), .CHANNEL_C(ay_c),
        .LEVEL_A(), .LEVEL_B(), .LEVEL_C(), .SEL(1'b0), .MODE(1'b1), .ACTIVE(),
        .IOA_in(8'hff), .IOA_out(), .IOB_in(8'hff), .IOB_out()
    );

    // Average away the unipolar DC offset at the PCM sample rate.
    reg [10:0] audio_div;
    reg signed [23:0] dc_offset;
    wire [9:0] beeper = (speaker ? 10'd192 : 10'd0) + (microphone ? 10'd48 : 10'd0);
    wire [9:0] ay_mix = mode_128 ? {2'b0, ay_a} + {2'b0, ay_b} + {2'b0, ay_c} : 10'd0;
    wire [9:0] mix = ay_mix + beeper;
    wire signed [23:0] sample_fixed = $signed({4'b0, mix, 10'b0});
    wire signed [23:0] sample_ac = sample_fixed - dc_offset;
    always @(posedge core_clk) begin
        audio_valid <= 0;
        if (reset) begin
            audio_div <= 0;
            dc_offset <= 0;
            audio_l <= 0;
            audio_r <= 0;
        end else if (audio_div == 11'd1166) begin
            audio_div <= 0;
            audio_valid <= 1;
            dc_offset <= machine_reset || pause_request ? 24'sd0 : dc_offset + (sample_ac >>> 9);
            audio_l <= machine_reset || pause_request ? 24'sd0 : sample_ac <<< 2;
            audio_r <= machine_reset || pause_request ? 24'sd0 : sample_ac <<< 2;
        end else audio_div <= audio_div + 1'b1;
    end
endmodule

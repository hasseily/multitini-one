// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// Native Vectrex and ARM-vector backend. Machine time is independent of DVI.
// All memories are writable through the framed common load stream; no BIOS
// or game content is embedded. The 64 KiB cartridge supports PB6 bank select.
module multitini_vectrex_machine #(
    parameter integer WIDTH=540, HEIGHT=720,
    parameter integer CLOCK_HZ=96000000,
    parameter integer ADDRESS_WIDTH=$clog2(WIDTH*HEIGHT)
) (
    input wire clk, reset, core_reset_request, pause_request, clear_load_error,
    input wire [15:0] input_code, input_value,
    input wire input_valid,
    output wire input_ready,
    input wire [7:0] load_data, load_kind,
    input wire load_valid, load_first, load_last,
    input wire load_abort, load_abort_epoch, load_epoch,
    input wire [31:0] load_offset, load_total,
    output wire load_ready,
    output reg ready,
    output reg load_error, bios_loaded, cart_loaded,
    output wire [31:0] core_id,
    output reg signed [23:0] audio_l, audio_r,
    output reg audio_valid,
    input wire pixel_clk,
    input wire [ADDRESS_WIDTH-1:0] pixel_address,
    output wire [7:0] pixel_intensity,
    output wire [15:0] debug_cpu_address,
    output wire debug_cpu_read, debug_cpu_write,
    output wire [7:0] debug_cpu_data,
    output wire signed [23:0] debug_beam_x, debug_beam_y,
    output wire debug_beam_valid,
    output wire [7:0] debug_beam_intensity,
    output wire debug_beam_blank,
    output wire [31:0] drawn_lines, overflow_count
);
    localparam [7:0] BIOS=8'h40, CART=8'h41, LINE=8'h42,
                     PSG=8'h43, MODE=8'h44;
    localparam integer CLOCKS_PER_CPU=CLOCK_HZ/1500000;
    localparam integer COORDINATE_FRACTION=$clog2(CLOCKS_PER_CPU);
    localparam integer AUDIO_DIVIDER=CLOCK_HZ/48000;
    localparam integer AUDIO_COUNTER_WIDTH=$clog2(AUDIO_DIVIDER);
    // Constant Q24 ratios avoid a wide combinational divider in the pixel
    // projection. Error is below 0.01 pixel across the integrator's range.
    localparam signed [31:0] X_SCALE=(64'd16777216*WIDTH+17688)/35376;
    localparam signed [31:0] Y_SCALE=(64'd16777216*HEIGHT+21384)/42768;
    assign core_id=32'h56454358;
    reg stream_active;
    reg active_epoch, aborted_epoch, aborted_valid;
    reg [7:0] stream_kind;
    reg [31:0] expected_offset, stream_total, cart_size;
    reg gs_mode;
    reg [7:0] command [0:7];
    reg line_pending, psg_pending;
    reg signed [15:0] arm_x0, arm_y0, arm_x1, arm_y1;
    reg [7:0] arm_intensity;
    reg [3:0] psg_register;
    reg [7:0] psg_value;
    reg [1:0] psg_phase;
    wire renderer_initialized;
    wire cpu_paused;
    wire loading_machine=stream_active && (stream_kind==BIOS || stream_kind==CART);
    wire machine_reset=reset || core_reset_request || loading_machine ||
                       (!bios_loaded && !gs_mode);
    wire cpu_reset=machine_reset || gs_mode || !renderer_initialized;
    wire cpu_tick, e_tick, cpu_valid, instruction_fetch;
    wire [111:0] cpu_registers;
    wire [15:0] cpu_address;
    wire [7:0] cpu_data_out;
    reg [7:0] cpu_data_in;
    wire cpu_read, cpu_write;
    wire via_select=cpu_address[15:11]==5'b11010;
    wire ram_select=cpu_address[15:11]==5'b11001;
    wire rom_select=cpu_address[15:13]==3'b111;
    wire cart_select=!cpu_address[15];
    wire [7:0] via_data, pa_bus, pb_bus, pa_out, pb_out;
    wire [7:0] pa_ddr, pb_ddr, pcr;
    wire via_irq, ca2, cb1, cb2;
    reg [7:0] buttons;
    reg signed [7:0] joy_x1, joy_y1, joy_x2, joy_y2;
    reg signed [7:0] selected_axis;
    // Comparator feedback occupies PB5 only. Resolve selector bits separately
    // so simulators need not settle an apparent whole-port combinational loop.
    wire [1:0] axis_select=(pb_out[2:1] & pb_ddr[2:1]) | ~pb_ddr[2:1];
    wire signed [7:0] threshold=pa_bus;
    wire [7:0] pb_input={2'b11,selected_axis>threshold,5'b11111};
    wire [7:0] psg_data, sound_a, sound_b, sound_c;
    wire [7:0] psg_ioa, psg_iob;
    wire ioa_oen, iob_oen;
    wire [5:0] psg_active;
    wire psg_bdir=gs_mode ? psg_phase!=0 : pb_bus[4];
    wire psg_bc=gs_mode ? psg_phase==1 : pb_bus[3];
    wire [7:0] psg_input=gs_mode ? (psg_phase==1 ? {4'd0,psg_register} : psg_value) : pa_bus;

    (* ram_style="block" *) reg [7:0] bios [0:8191];
    (* ram_style="block" *) reg [7:0] cartridge [0:65535];
    (* ram_style="block" *) reg [7:0] ram [0:1023];
    reg [7:0] bios_read, cart_read, ram_read;
    wire [15:0] cart_address={cart_size>32768 && pb_bus[6],cpu_address[14:0]};
    reg [10:0] ram_clear;
    wire ram_initializing=!ram_clear[10];
    // One explicit write port permits block-RAM inference even though cold
    // initialization and CPU writes use different address sources.
    wire ram_write=ram_initializing ? !reset : cpu_write && ram_select;
    wire [9:0] ram_write_address=ram_initializing ? ram_clear[9:0] : cpu_address[9:0];
    wire [7:0] ram_write_data=ram_initializing ? 8'd0 : cpu_data_out;
    wire correct_length=(load_kind==BIOS && load_total==8192) ||
        (load_kind==CART && load_total>0 && load_total<=65536) ||
        (load_kind==LINE && load_total==9 && gs_mode) ||
        (load_kind==PSG && load_total==2 && gs_mode) ||
        (load_kind==MODE && load_total==1);
    wire starts_object=!stream_active || (load_first && load_epoch!=active_epoch);
    wire stale_byte=(aborted_valid && load_epoch==aborted_epoch) ||
                    (load_abort && load_epoch==load_abort_epoch);
    wire framing_good=load_offset<load_total &&
        load_last==(load_offset+1==load_total) &&
        (!starts_object ? (!load_first && load_kind==stream_kind && load_epoch==active_epoch &&
            load_total==stream_total && load_offset==expected_offset) :
            (load_first && load_offset==0 && correct_length));
    assign load_ready=!reset && !line_pending && !psg_pending;
    wire transfer=load_ready && load_valid;
    wire write_byte=transfer && framing_good && !stale_byte;
    // Export a registered level to the control/video synchronizers. Combining
    // independently changing readiness terms at the CDC input can glitch.
    always @(posedge clk) begin
        if (reset) ready<=1'b0;
        else ready<=renderer_initialized && !ram_initializing &&
                    (bios_loaded || gs_mode) && !loading_machine && !load_error;
    end
    assign input_ready=!reset;

    always @(posedge clk) begin
        bios_read<=bios[cpu_address[12:0]];
        cart_read<=cartridge[cart_address];
        ram_read<=ram[cpu_address[9:0]];
        if (write_byte && load_kind==BIOS) bios[load_offset[12:0]]<=load_data;
        if (write_byte && load_kind==CART) cartridge[load_offset[15:0]]<=load_data;
        if (ram_write) ram[ram_write_address]<=ram_write_data;
    end
    always @* begin
        cpu_data_in=8'hff;
        if (rom_select) cpu_data_in=bios_read;
        else if (ram_select) cpu_data_in=ram_read;
        else if (via_select) cpu_data_in=via_data;
        else if (cart_select && cart_loaded && {16'd0,cart_address}<cart_size)
            cpu_data_in=cart_read;
        case (axis_select)
            0: selected_axis=joy_x1;
            1: selected_axis=joy_y1;
            2: selected_axis=joy_x2;
            3: selected_axis=joy_y2;
        endcase
    end
    multitini_vectrex_cpu #(.CLOCKS_PER_CPU(CLOCKS_PER_CPU)) cpu_i (
        .clk(clk),.reset(cpu_reset || ram_initializing),.pause_request(pause_request),
        .paused(cpu_paused),.irq(via_irq),.firq(1'b0),.nmi(1'b0),
        .data_in(cpu_data_in),.address(cpu_address),.data_out(cpu_data_out),
        .bus_read(cpu_read),.bus_write(cpu_write),.cpu_tick(cpu_tick),.e_tick(e_tick),
        .bus_valid(cpu_valid),.instruction_fetch(instruction_fetch),.debug_registers(cpu_registers)
    );
    multitini_vectrex_via6522 #(.TIMER_READ_PRETICK(0)) via_i (
        .clk(clk),.reset(machine_reset || gs_mode),.power_reset(reset),
        .data_in(cpu_data_out),.data_out(via_data),.addr(cpu_address[3:0]),
        .strobe(via_select && (cpu_read || cpu_write)),.we(cpu_write),
        .slow_clock(e_tick),.timer_read_extra_clock(1'b0),
        .ifr_set_ext(7'd0),.ifr_clr_ext(7'd0),.irq(via_irq),
        .porta_in(psg_data),.porta_out(pa_out),.porta_bus(pa_bus),.ddra_out(pa_ddr),
        .portb_in(pb_input),.portb_out(pb_out),.portb_bus(pb_bus),.ddrb_out(pb_ddr),
        .pcr_out(pcr),.ca1_in(1'b1),.ca2_in(1'b1),.ca2_out(ca2),
        .cb1_in(1'b1),.cb1_out(cb1),.cb2_in(1'b1),.cb2_out(cb2)
    );
    reg [COORDINATE_FRACTION-1:0] psg_divider;
    wire psg_ce=psg_divider==CLOCKS_PER_CPU-1;
    always @(posedge clk) begin
        if (reset) psg_divider<=0;
        else if (!pause_request)
            psg_divider<=psg_ce ? 0 : psg_divider+1'b1;
    end
    multitini_vectrex_psg #(.IO_PORTS(1)) psg_i (
        .CLK(clk),.CE(psg_ce && !pause_request),.RESET(machine_reset),
        .BDIR(psg_bdir),.BC(psg_bc),.DI(psg_input),.DO(psg_data),
        .CHANNEL_A(sound_a),.CHANNEL_B(sound_b),.CHANNEL_C(sound_c),
        .SEL(1'b0),.MODE(1'b1),.ACTIVE(psg_active),
        .IOA_in(~buttons),.IOA_out(psg_ioa),.IOA_OEn(ioa_oen),
        .IOB_in(8'hff),.IOB_out(psg_iob),.IOB_OEn(iob_oen)
    );

    wire signed [23:0] beam_x, beam_y;
    wire [7:0] beam_intensity;
    wire beam_blank, beam_valid;
    wire signed [7:0] dac_audio;
    multitini_vectrex_beam #(.ANALOG_DELAY_CLOCKS(CLOCK_HZ/1000000*85/10)) beam_i (
        .clk(clk),.reset(machine_reset || gs_mode),.pause(cpu_paused),
        .sample_tick(cpu_tick),.porta(pa_bus),.portb(pb_bus),.ca2(ca2),.cb2(cb2),
        .x(beam_x),.y(beam_y),.intensity(beam_intensity),.blank(beam_blank),
        .point_valid(beam_valid),.dac_audio(dac_audio)
    );
    // Coordinate projection is performed before queueing so the renderer only
    // traverses bounded screen-space segments. Keep one screen margin for
    // crossing vectors; reject entirely off-screen spans in the renderer.
    function automatic signed [15:0] project_x(input signed [31:0] coordinate);
        reg signed [63:0] scaled;
        begin
            scaled=((coordinate*X_SCALE)>>>24)+WIDTH/2;
            if (scaled < -WIDTH) project_x=-WIDTH;
            else if (scaled > WIDTH*2) project_x=WIDTH*2;
            else project_x=scaled[15:0];
        end
    endfunction
    function automatic signed [15:0] project_y(input signed [31:0] coordinate);
        reg signed [63:0] scaled;
        begin
            scaled=HEIGHT/2-((coordinate*Y_SCALE)>>>24);
            if (scaled < -HEIGHT) project_y=-HEIGHT;
            else if (scaled > HEIGHT*2) project_y=HEIGHT*2;
            else project_y=scaled[15:0];
        end
    endfunction
    reg signed [15:0] previous_x, previous_y, native_x0,native_y0,native_x1,native_y1;
    reg previous_blank;
    reg [7:0] previous_intensity, native_intensity;
    reg native_valid;
    wire renderer_ready;
    wire line_consumed=gs_mode && line_pending && renderer_ready;
    always @(posedge clk) begin
        native_valid<=1'b0;
        if (cpu_reset) begin
            previous_x<=WIDTH/2; previous_y<=HEIGHT/2; previous_blank<=1'b1;
            previous_intensity<=0; native_intensity<=0;
            native_x0<=0;native_y0<=0;native_x1<=0;native_y1<=0;
        end else if (beam_valid) begin
            native_x0<=previous_x; native_y0<=previous_y;
            native_x1<=project_x($signed(beam_x)>>>COORDINATE_FRACTION);
            native_y1<=project_y($signed(beam_y)>>>COORDINATE_FRACTION);
            native_intensity<=previous_intensity;
            native_valid<=!previous_blank;
            previous_x<=project_x($signed(beam_x)>>>COORDINATE_FRACTION);
            previous_y<=project_y($signed(beam_y)>>>COORDINATE_FRACTION);
            previous_blank<=beam_blank;previous_intensity<=beam_intensity;
        end
    end
    multitini_vectrex_renderer #(.WIDTH(WIDTH),.HEIGHT(HEIGHT),.CLOCK_HZ(CLOCK_HZ)) renderer_i (
        .clk(clk),.reset(reset || core_reset_request),.pause(pause_request),
        .line_x0(gs_mode ? project_x(arm_x0) : native_x0),
        .line_y0(gs_mode ? project_y(arm_y0) : native_y0),
        .line_x1(gs_mode ? project_x(arm_x1) : native_x1),
        .line_y1(gs_mode ? project_y(arm_y1) : native_y1),
        .line_intensity(gs_mode ? arm_intensity : native_intensity),
        .line_valid(gs_mode ? (line_pending && renderer_ready) : native_valid),
        .line_ready(renderer_ready),.initialized(renderer_initialized),
        .drawn_lines(drawn_lines),.overflow_count(overflow_count),
        .pixel_clk(pixel_clk),.pixel_address(pixel_address),.pixel_intensity(pixel_intensity)
    );
    reg [AUDIO_COUNTER_WIDTH-1:0] audio_counter;
    reg signed [15:0] mix;
    always @* mix=$signed({1'b0,sound_a})+$signed({1'b0,sound_b})+
                 $signed({1'b0,sound_c})+($signed(dac_audio)<<<1);
    always @(posedge clk) begin
        audio_valid<=1'b0;
        if (reset) begin audio_counter<=0;audio_l<=0;audio_r<=0; end
        else if (audio_counter==AUDIO_DIVIDER-1) begin
            audio_counter<=0;audio_valid<=1'b1;
            audio_l<=pause_request || !ready ? 24'sd0 : $signed(mix)<<<12;
            audio_r<=pause_request || !ready ? 24'sd0 : $signed(mix)<<<12;
        end else audio_counter<=audio_counter+1'b1;
    end
    always @(posedge clk) begin
        if (reset) begin
            bios_loaded<=0;cart_loaded<=0;cart_size<=0;load_error<=0;
            stream_active<=0;stream_kind<=0;expected_offset<=0;stream_total<=0;
            active_epoch<=0;aborted_epoch<=0;aborted_valid<=0;
            gs_mode<=0;line_pending<=0;psg_pending<=0;psg_phase<=0;
            arm_x0<=0;arm_y0<=0;arm_x1<=0;arm_y1<=0;arm_intensity<=0;
            psg_register<=0;psg_value<=0;ram_clear<=0;
            buttons<=0;joy_x1<=0;joy_y1<=0;joy_x2<=0;joy_y2<=0;
        end else begin
            if (ram_initializing) ram_clear<=ram_clear+1'b1;
            if (clear_load_error) begin load_error<=0;stream_active<=0; end
            if (load_abort) begin
                aborted_epoch<=load_abort_epoch;aborted_valid<=1;
                if (!stream_active || active_epoch==load_abort_epoch) stream_active<=0;
            end
            if (line_consumed) line_pending<=0;
            if (psg_pending) begin
                if (psg_phase==0) psg_phase<=1;
                else if (psg_phase==1) psg_phase<=2;
                else begin psg_phase<=0;psg_pending<=0; end
            end
            if (input_valid && !core_reset_request && input_code[15:8]==8'h20) begin
                case (input_code[7:0])
                    0: buttons<=input_value[7:0];
                    1: joy_x1<=input_value[15:8];
                    2: joy_y1<=input_value[15:8];
                    3: joy_x2<=input_value[15:8];
                    4: joy_y2<=input_value[15:8];
                    default: begin end
                endcase
            end
            if (transfer && !stale_byte) begin
                if (load_first && load_kind==BIOS) bios_loaded<=0;
                if (load_first && load_kind==CART) begin cart_loaded<=0;cart_size<=0;end
                if (!framing_good) begin
                    load_error<=1;stream_active<=0;
                end else begin
                    stream_active<=!load_last;stream_kind<=load_kind;
                    active_epoch<=load_epoch;
                    if (load_first) aborted_valid<=0;
                    stream_total<=load_total;expected_offset<=load_offset+1;
                    if (load_kind==LINE || load_kind==PSG)
                        if (load_offset<8) command[load_offset[2:0]]<=load_data;
                    if (load_last) begin
                        case (load_kind)
                            BIOS: bios_loaded<=1;
                            CART: begin cart_loaded<=1;cart_size<=load_total;gs_mode<=0;end
                            LINE: begin
                                arm_x0<={command[1],command[0]}; arm_y0<={command[3],command[2]};
                                arm_x1<={command[5],command[4]}; arm_y1<={command[7],command[6]};
                                arm_intensity<=load_data;line_pending<=1;
                            end
                            PSG: begin
                                if (command[0]>15) load_error<=1;
                                else begin psg_register<=command[0][3:0];psg_value<=load_data;psg_pending<=1;end
                            end
                            MODE: begin
                                if (load_data>2) load_error<=1;
                                else begin
                                    gs_mode<=load_data==1;
                                    if (load_data==2) begin cart_loaded<=0;cart_size<=0;end
                                end
                            end
                            default: load_error<=1;
                        endcase
                    end
                end
            end
            // The renderer is held in reset and cannot consume a queued ARM
            // line. Discard pending peripheral work so the host can load a
            // new mode/BIOS/cartridge while core reset remains asserted.
            // Framed transfers retain their separate explicit abort protocol.
            if (core_reset_request) begin
                line_pending<=0;psg_pending<=0;psg_phase<=0;
            end
        end
    end
    assign debug_cpu_address=cpu_address;
    assign debug_cpu_read=cpu_read;assign debug_cpu_write=cpu_write;
    assign debug_cpu_data=cpu_data_out;
    assign debug_beam_x=beam_x;assign debug_beam_y=beam_y;
    assign debug_beam_valid=beam_valid;assign debug_beam_intensity=beam_intensity;
    assign debug_beam_blank=beam_blank;
endmodule

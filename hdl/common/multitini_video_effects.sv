// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// CPU1 prepares a complete color transform; this bounded streaming pipeline
// applies it without frame storage, CPU pixel copies, or game-clock stalls.
// Both LUT banks have independent control-write / pixel-read ports. A coherent
// mailbox transfers coefficients and bank ownership only at vertical blank.
module multitini_video_effects #(
    parameter bit DYNAMIC_DISPLAY=1'b0,
    parameter integer SOURCE_SCALE_NUM = 4,
    parameter integer SOURCE_SCALE_DEN = 1,
    parameter bit SHARED_PIXEL_RESET = 1'b0,
    parameter bit SHARED_PIXEL_LIVENESS = 1'b0
) (
    input wire [3:0] display_scale_num,display_scale_den,
    input wire control_clk, control_resetn,
    input wire effect_write,
    input wire [9:0] effect_address,
    input wire [31:0] effect_data,
    input wire effect_commit,
    input wire [31:0] effect_commit_sequence,
    output wire effect_busy,
    output reg effect_active_valid,
    output reg effect_rejected,
    output reg [31:0] effect_active_sequence,
    input wire pixel_clk, pixel_resetn, shared_pixel_resetn, shared_pixel_running, vblank_start,
    input wire [15:0] pixel_rgb565,
    input wire [15:0] pixel_neighbor_rgb565,
    input wire [7:0] pixel_neighbor_weight,
    input wire pixel_effect_enable,
    input wire [11:0] pixel_x, pixel_y,
    input wire pixel_de, pixel_hsync, pixel_vsync,
    output reg [15:0] output_rgb565,
    output reg output_de, output_hsync, output_vsync
);
    localparam integer PAYLOAD_WIDTH = 184; // sequence32, bank1, config7, matrix144
    // PS reset crosses once through an async-assert/sync-release chain.
    // Pixel-only reset invalidates banks while live mailboxes drain; no raw
    // reset signal or combinational reset tree crosses either clock domain.
    wire mailbox_pixel_run;
    generate if(SHARED_PIXEL_RESET)begin : g_shared_reset
        assign mailbox_pixel_run=shared_pixel_resetn;
    end else begin : g_local_reset
    (* ASYNC_REG = "TRUE" *) reg [2:0] mailbox_pixel_release;
    always @(posedge pixel_clk or negedge control_resetn)
        if (!control_resetn) mailbox_pixel_release <= 0;
        else mailbox_pixel_release <= {mailbox_pixel_release[1:0],1'b1};
    assign mailbox_pixel_run = mailbox_pixel_release[2];
    end endgenerate
    reg [2:0] pixel_release;
    always @(posedge pixel_clk)
        if (!mailbox_pixel_run || !pixel_resetn) pixel_release <= 0;
        else pixel_release <= {pixel_release[1:0], 1'b1};
    wire pixel_run = pixel_release[2];
    wire control_run;
    generate if(SHARED_PIXEL_LIVENESS)begin : g_shared_liveness
        assign control_run=shared_pixel_running;
    end else begin : g_local_liveness
    (* ASYNC_REG = "TRUE" *) reg [2:0] control_release;
    always @(posedge control_clk or negedge control_resetn)
        if (!control_resetn) control_release <= 0;
        else control_release <= {control_release[1:0], pixel_run};
    assign control_run = control_release[2];
    end endgenerate

    reg signed [15:0] coefficient_bank [0:1][0:8];
    reg [6:0] config_bank [0:1];
    (* ram_style = "block" *) reg [7:0] red_lut [0:511];
    (* ram_style = "block" *) reg [7:0] green_lut [0:511];
    (* ram_style = "block" *) reg [7:0] blue_lut [0:511];
    reg [268:0] written;
    reg active_bank_control, pending, started, launch;
    reg [PAYLOAD_WIDTH-1:0] payload;
    wire mailbox_ready, destination_valid;
    wire [PAYLOAD_WIDTH-1:0] destination;
    wire write_bank = !active_bank_control;
    // Keep the XPM handshake alive through a pixel-only reset. Recover by
    // acknowledging any old request without applying it before either side
    // may reuse a bank. XPM itself has no reset port.
    reg recovering;
    reg [3:0] recovery_cycles;
    reg mailbox_ready_launch;
    always @(posedge control_clk)
        if (!control_resetn) mailbox_ready_launch <= 0;
        else mailbox_ready_launch <= mailbox_ready;
    (* ASYNC_REG = "TRUE" *) reg [1:0] mailbox_idle_pixel, recovered_control;
    always @(posedge pixel_clk) begin
        if(!pixel_run) begin
            mailbox_idle_pixel<=0;recovering<=1;recovery_cycles<=0;
        end else begin
            mailbox_idle_pixel<={mailbox_idle_pixel[0],mailbox_ready_launch};
            if(recovery_cycles!=15)recovery_cycles<=recovery_cycles+1'b1;
            if(recovery_cycles==15 && mailbox_idle_pixel[1])recovering<=0;
        end
    end
    always @(posedge control_clk)
        if(!control_run)recovered_control<=0;
        else recovered_control<={recovered_control[0],!recovering};
    assign effect_busy = !control_run || !recovered_control[1] || pending || !mailbox_ready;
    wire table_address = effect_address >= 10'h100 && effect_address <= 10'h1ff;
    wire register_address = effect_address <= 10'd12;
    wire reserved_value_bad = (effect_address >= 9 && effect_address <= 11 && effect_data != 0) ||
                             (effect_address == 12 && effect_data[31:7] != 0) ||
                             (effect_address <= 8 && effect_data[31:16] != 0);
    wire write_accepted = control_run && effect_write && !effect_busy &&
                          (table_address || register_address) && !reserved_value_bad;
    wire commit_accepted = effect_commit && !effect_write && !effect_busy && (&written);
    integer ci;
    always @(posedge control_clk) begin
        // Do not reset RAM contents: the complete-write bitmap, ownership
        // handshake and initial bypass make uninitialized entries unreachable.
        if (write_accepted && table_address) begin
            red_lut[{write_bank,effect_address[7:0]}] <= effect_data[7:0];
            green_lut[{write_bank,effect_address[7:0]}] <= effect_data[15:8];
            blue_lut[{write_bank,effect_address[7:0]}] <= effect_data[23:16];
        end
        if (!control_run) begin
            written <= 0;
            active_bank_control <= 0;
            pending <= 0; started <= 0; launch <= 0; payload <= 0;
            effect_active_valid <= 0; effect_rejected <= 0; effect_active_sequence <= 0;
        end else begin
            launch <= 0;
            if (effect_write) begin
                if (!write_accepted) effect_rejected <= 1;
                else if (table_address) written[13+effect_address[7:0]] <= 1;
                else begin
                    written[effect_address] <= 1;
                    if (effect_address < 9)
                        coefficient_bank[write_bank][effect_address] <= effect_data[15:0];
                    if (effect_address == 12) config_bank[write_bank] <= effect_data[6:0];
                end
            end
            if (effect_commit) begin
                if (!commit_accepted) effect_rejected <= 1;
                else begin
                    for (ci=0;ci<9;ci=ci+1)
                        payload[ci*16+:16] <= coefficient_bank[write_bank][ci];
                    payload[150:144] <= config_bank[write_bank];
                    payload[151] <= write_bank;
                    payload[183:152] <= effect_commit_sequence;
                    pending <= 1; started <= 0; launch <= 1;
                    written <= 0;
                end
            end
            if (pending && !mailbox_ready) started <= 1;
            if (pending && started && mailbox_ready) begin
                pending <= 0;
                active_bank_control <= payload[151];
                effect_active_sequence <= payload[183:152];
                effect_active_valid <= 1;
            end
        end
    end

    multitini_async_mailbox #(.WIDTH(PAYLOAD_WIDTH),.DRAIN_ON_RESET(1'b1)) commit_mailbox (
        .src_clk(control_clk), .src_resetn(control_resetn), .src_data(payload),
        .src_valid(launch), .src_ready(mailbox_ready),
        .dst_clk(pixel_clk), .dst_resetn(mailbox_pixel_run), .dst_data(destination),
        .dst_valid(destination_valid), .dst_ready(!pixel_run || recovering || vblank_start)
    );
    reg signed [15:0] matrix [0:8];
    reg [6:0] spatial;
    reg active_bank_pixel, active_valid_pixel;
    integer mi;
    always @(posedge pixel_clk) begin
        if (!pixel_run) begin
            active_bank_pixel <= 0; active_valid_pixel <= 0; spatial <= 0;
            for (mi=0;mi<9;mi=mi+1) matrix[mi] <= 0;
        end else if (destination_valid && vblank_start && !recovering) begin
            active_bank_pixel <= destination[151];
            spatial <= destination[150:144];
            active_valid_pixel <= 1;
            for (mi=0;mi<9;mi=mi+1) matrix[mi] <= destination[mi*16+:16];
        end
    end

    // Register the fully composed pixel before the DSPs. Native BRAM scanout,
    // DDR cache selection, borders and text all end at this common boundary.
    // Every pixel and its timing/viewport metadata therefore gains one cycle.
    reg [15:0] sample_rgb565, sample_neighbor_rgb565;
    reg [7:0] sample_weight;
    reg sample_enable,sample_de,sample_hsync,sample_vsync;
    reg [11:0] sample_x,sample_y;
    always @(posedge pixel_clk)begin
        if(!pixel_run)begin
            sample_rgb565<=0;sample_neighbor_rgb565<=0;sample_weight<=0;
            sample_enable<=0;sample_de<=0;sample_hsync<=0;sample_vsync<=0;
            sample_x<=0;sample_y<=0;
        end else begin
            sample_rgb565<=pixel_rgb565;sample_enable<=pixel_effect_enable;
            sample_neighbor_rgb565<=pixel_neighbor_rgb565;
            if(!pixel_effect_enable || !active_valid_pixel || !spatial[6])sample_weight<=0;
            // Strength is applied once by the native horizontal prefilter.
            // Every enabled level uses full, bounded centered interpolation.
            else sample_weight<=spatial[5:4]!=0 ? pixel_neighbor_weight : 8'd0;
            sample_de<=pixel_de;sample_hsync<=pixel_hsync;sample_vsync<=pixel_vsync;
            sample_x<=pixel_x;sample_y<=pixel_y;
        end
    end
    wire [7:0] center_channels[0:2],neighbor_channels[0:2];
    assign center_channels[0]={sample_rgb565[15:11],sample_rgb565[15:13]};
    assign center_channels[1]={sample_rgb565[10:5],sample_rgb565[10:9]};
    assign center_channels[2]={sample_rgb565[4:0],sample_rgb565[4:2]};
    assign neighbor_channels[0]={sample_neighbor_rgb565[15:11],sample_neighbor_rgb565[15:13]};
    assign neighbor_channels[1]={sample_neighbor_rgb565[10:5],sample_neighbor_rgb565[10:9]};
    assign neighbor_channels[2]={sample_neighbor_rgb565[4:0],sample_neighbor_rgb565[4:2]};
    (* use_dsp="yes" *) reg signed [17:0] blend_products[0:2];
    reg [7:0] blend_centers[0:2],blended[0:2];
    // Quantize the interpolated RGB8 result to RGB565 before color processing,
    // matching native frame storage and the software screenshot reference.
    wire [7:0] input_red = {blended[0][7:3],blended[0][7:5]};
    wire [7:0] input_green = {blended[1][7:2],blended[1][7:6]};
    wire [7:0] input_blue = {blended[2][7:3],blended[2][7:5]};
    (* use_dsp = "yes" *) reg signed [24:0] products [0:8];
    reg signed [26:0] sums [0:2];
    reg [7:0] indices [0:2];
    reg [7:0] tone_ram [0:2], tone [0:2];
    reg [15:0] bypass [0:6];
    reg [6:0] enables, de_pipe, hs_pipe, vs_pipe;
    reg [11:0] x_pipe [0:6], y_pipe [0:6];
    reg [1:0] x_phase [0:6], y_phase [0:6];
    reg [3:0] x_mod_lo,x_mod_hi,y_mod_lo,y_mod_hi;
    reg [4:0] x_mod_sum,y_mod_sum;
    reg [6:0] scanline_pipe;
    reg [6:0] config_pipe [0:6];
    integer pi;

    // 4 == 1 (mod 3). Sum six base-four digits in two short stages,
    // then reduce the bounded 0..18 result. This avoids an inferred wide
    // divide/modulo chain on the 148.5 MHz coordinate path.
    function automatic [1:0] modulo_three(input [4:0] sum);
        case(sum)
            1,4,7,10,13,16:modulo_three=1;
            2,5,8,11,14,17:modulo_three=2;
            default:modulo_three=0;
        endcase
    endfunction

    function automatic [7:0] clamp_sum(input signed [26:0] value);
        reg signed [26:0] rounded;
        begin
            rounded = (value + 27'sd512) >>> 10;
            if (rounded < 0) clamp_sum = 0;
            else if (rounded > 255) clamp_sum = 255;
            else clamp_sum = rounded[7:0];
        end
    endfunction
    function automatic [7:0] blend_round(input [7:0] center, input signed [17:0] product);
        reg signed [17:0] sum;
        begin
            sum=$signed({2'b0,center,8'b0})+product+18'sd128;
            blend_round=sum[15:8];
        end
    endfunction
    function automatic [7:0] spatial_channel(input [7:0] value, input integer channel,
        input [1:0] phase, row_phase, input row_odd, dark_line, input [6:0] configuration);
        reg [2:0] selected;
        reg [9:0] scaled;
        reg [7:0] shaded;
        begin
            scaled = {2'b0,value};
            if (dark_line) begin
                case (configuration[1:0])
                    1: scaled = (({2'b0,value} << 1) + value) >> 2;
                    2: scaled = value >> 1;
                    3: scaled = value >> 2;
                    default: scaled = value;
                endcase
            end
            shaded = scaled[7:0];
            selected = {1'b0,phase};
            if (configuration[3:2] == 2 && row_odd)
                selected = phase == 2 ? 0 : selected + 1'b1;
            if (((configuration[3:2] == 1 || configuration[3:2] == 2) && selected != channel) ||
                (configuration[3:2] == 3 && (phase == 2 || row_phase == 2)))
                shaded = shaded - (shaded >> 2);
            spatial_channel = shaded;
        end
    endfunction
    wire [7:0] final_red = spatial_channel(tone[0],0,x_phase[6],y_phase[6],y_pipe[6][0],scanline_pipe[6],config_pipe[6]);
    wire [7:0] final_green = spatial_channel(tone[1],1,x_phase[6],y_phase[6],y_pipe[6][0],scanline_pipe[6],config_pipe[6]);
    wire [7:0] final_blue = spatial_channel(tone[2],2,x_phase[6],y_phase[6],y_pipe[6][0],scanline_pipe[6],config_pipe[6]);
    // RAM read registers intentionally have no reset for BRAM inference.
    always @(posedge pixel_clk) begin
        tone_ram[0] <= red_lut[{active_bank_pixel,indices[0]}];
        tone_ram[1] <= green_lut[{active_bank_pixel,indices[1]}];
        tone_ram[2] <= blue_lut[{active_bank_pixel,indices[2]}];
    end
    always @(posedge pixel_clk) begin
        if (!pixel_run) begin
            enables <= 0; de_pipe <= 0; hs_pipe <= 0; vs_pipe <= 0;
            scanline_pipe <= 0;
            output_rgb565 <= 0; output_de <= 0; output_hsync <= 0; output_vsync <= 0;
            x_mod_lo<=0;x_mod_hi<=0;y_mod_lo<=0;y_mod_hi<=0;
            x_mod_sum<=0;y_mod_sum<=0;
            for (pi=0;pi<7;pi=pi+1) begin
                bypass[pi]<=0; x_pipe[pi]<=0; y_pipe[pi]<=0; config_pipe[pi]<=0;
                x_phase[pi]<=0; y_phase[pi]<=0;
            end
            for (pi=0;pi<9;pi=pi+1) products[pi]<=0;
            for (pi=0;pi<3;pi=pi+1) begin
                sums[pi]<=0; indices[pi]<=0; tone[pi]<=0;
                blend_products[pi]<=0;blend_centers[pi]<=0;blended[pi]<=0;
            end
        end else begin
            enables <= {enables[5:0],sample_enable && active_valid_pixel && spatial[6]};
            de_pipe <= {de_pipe[5:0],sample_de};
            hs_pipe <= {hs_pipe[5:0],sample_hsync};
            vs_pipe <= {vs_pipe[5:0],sample_vsync};
            bypass[0] <= sample_rgb565; x_pipe[0] <= sample_x; y_pipe[0] <= sample_y;
            config_pipe[0] <= spatial;
            x_mod_lo <= {2'b0,sample_x[1:0]}+{2'b0,sample_x[3:2]}+{2'b0,sample_x[5:4]};
            x_mod_hi <= {2'b0,sample_x[7:6]}+{2'b0,sample_x[9:8]}+{2'b0,sample_x[11:10]};
            y_mod_lo <= {2'b0,sample_y[1:0]}+{2'b0,sample_y[3:2]}+{2'b0,sample_y[5:4]};
            y_mod_hi <= {2'b0,sample_y[7:6]}+{2'b0,sample_y[9:8]}+{2'b0,sample_y[11:10]};
            x_mod_sum <= {1'b0,x_mod_lo}+{1'b0,x_mod_hi};
            y_mod_sum <= {1'b0,y_mod_lo}+{1'b0,y_mod_hi};
            x_phase[2] <= modulo_three(x_mod_sum);
            y_phase[2] <= modulo_three(y_mod_sum);
            scanline_pipe[6:3] <= scanline_pipe[5:2];
            if(DYNAMIC_DISPLAY)begin
                if(display_scale_den==2)scanline_pipe[2]<=modulo_three(y_mod_sum)==1;
                else case(display_scale_num)
                    1:scanline_pipe[2]<=0;
                    2:scanline_pipe[2]<=y_pipe[1][0];
                    3:scanline_pipe[2]<=modulo_three(y_mod_sum)==2;
                    4:scanline_pipe[2]<=y_pipe[1][1];
                    5:scanline_pipe[2]<=(y_pipe[1] % 5)>=3;
                    default:scanline_pipe[2]<=y_pipe[1][0] ? modulo_three(y_mod_sum)!=1 : modulo_three(y_mod_sum)==1;
                endcase
            end else if(SOURCE_SCALE_DEN==2)scanline_pipe[2]<=modulo_three(y_mod_sum)==1;
            else if(SOURCE_SCALE_NUM==6)
                scanline_pipe[2]<=y_pipe[1][0] ? modulo_three(y_mod_sum)!=1 : modulo_three(y_mod_sum)==1;
            else scanline_pipe[2]<=y_pipe[1][1];
            for (pi=1;pi<7;pi=pi+1) begin
                bypass[pi]<=bypass[pi-1]; x_pipe[pi]<=x_pipe[pi-1]; y_pipe[pi]<=y_pipe[pi-1];
                config_pipe[pi]<=config_pipe[pi-1];
                if(pi>=3)begin x_phase[pi]<=x_phase[pi-1]; y_phase[pi]<=y_phase[pi-1];end
            end
            for (pi=0;pi<3;pi=pi+1) begin
                products[pi*3] <= matrix[pi*3] * $signed({1'b0,input_red});
                products[pi*3+1] <= matrix[pi*3+1] * $signed({1'b0,input_green});
                products[pi*3+2] <= matrix[pi*3+2] * $signed({1'b0,input_blue});
                sums[pi] <= {{2{products[pi*3][24]}},products[pi*3]} +
                            {{2{products[pi*3+1][24]}},products[pi*3+1]} +
                            {{2{products[pi*3+2][24]}},products[pi*3+2]};
                indices[pi] <= clamp_sum(sums[pi]);
                // Two pre-color stages replace the old post-color neighbor
                // stages. Throughput and the nine-clock timing stay unchanged.
                blend_products[pi] <= sample_weight==0 ? 18'sd0 :
                    ($signed({1'b0,neighbor_channels[pi]})-$signed({1'b0,center_channels[pi]})) *
                    $signed({1'b0,sample_weight});
                blend_centers[pi] <= center_channels[pi];
                blended[pi] <= blend_round(blend_centers[pi],blend_products[pi]);
                tone[pi] <= tone_ram[pi];
            end
            output_rgb565 <= enables[6] ? {final_red[7:3],final_green[7:2],final_blue[7:3]} : bypass[6];
            output_de <= de_pipe[6]; output_hsync <= hs_pipe[6]; output_vsync <= vs_pipe[6];
        end
    end
endmodule

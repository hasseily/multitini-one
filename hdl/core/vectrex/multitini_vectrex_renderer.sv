// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// Single intensity store shared by a queued vector writer and independent DVI
// scanout. A bounded background sweep fades phosphor in machine time. Both
// pixels and vectors are explicit interfaces, usable by the native machine
// and the ARM GS BASIC backend. No CPU clock is derived from display refresh.
module multitini_vectrex_renderer #(
    parameter integer WIDTH=540, HEIGHT=720,
    parameter integer CLOCK_HZ=96000000,
    parameter integer DECAY_HZ=60,
    parameter integer QUEUE_DEPTH=64,
    parameter integer ADDRESS_WIDTH=$clog2(WIDTH*HEIGHT)
) (
    input wire clk, reset, pause,
    input wire signed [15:0] line_x0, line_y0, line_x1, line_y1,
    input wire [7:0] line_intensity,
    input wire line_valid,
    output wire line_ready,
    output reg initialized,
    output reg [31:0] drawn_lines,
    output reg [31:0] overflow_count,
    input wire pixel_clk,
    input wire [ADDRESS_WIDTH-1:0] pixel_address,
    output reg [7:0] pixel_intensity
);
    localparam integer PIXELS=WIDTH*HEIGHT;
    localparam integer QW=$clog2(QUEUE_DEPTH);
    localparam [2:0] CLEAR=0, IDLE=1, DRAW_READ=2, DRAW_WRITE=3,
                     FADE_READ=4, FADE_WRITE=5, DRAW_BLEND=6;
    (* ram_style="block" *) reg [7:0] framebuffer [0:PIXELS-1];
    (* ram_style="distributed" *) reg [71:0] queue [0:QUEUE_DEPTH-1];
    reg [QW-1:0] write_pointer, read_pointer;
    reg [QW:0] queued;
    reg [2:0] state;
    reg [ADDRESS_WIDTH-1:0] clear_address, fade_address;
    reg [31:0] fade_phase;
    reg [15:0] fade_debt;
    reg signed [16:0] px, py, end_x, end_y;
    reg signed [17:0] delta_x, delta_y, error_term;
    reg signed [1:0] step_x, step_y;
    reg [7:0] stroke_intensity;
    reg [7:0] memory_read;
    reg [7:0] draw_data, fade_data;
    reg [ADDRESS_WIDTH-1:0] memory_address;
    reg memory_write;
    reg [7:0] memory_data;
    wire in_bounds=px>=0 && px<WIDTH && py>=0 && py<HEIGHT;
    wire fade_due=!pause && fade_phase >= CLOCK_HZ-PIXELS*DECAY_HZ;
    wire take_fade=state==IDLE && !pause && fade_debt!=0 &&
                   (queued==0 || fade_debt>=16);
    wire pop=state==IDLE && queued!=0 && !take_fade && !pause;
    wire push=line_valid && line_ready;
    wire fade_complete=state==FADE_WRITE;
    wire [71:0] next_line=queue[read_pointer];
    wire signed [15:0] next_x0=next_line[71:56];
    wire signed [15:0] next_y0=next_line[55:40];
    wire signed [15:0] next_x1=next_line[39:24];
    wire signed [15:0] next_y1=next_line[23:8];
    assign line_ready=initialized && queued<QUEUE_DEPTH && !reset;

    always @(posedge pixel_clk)
        pixel_intensity <= framebuffer[pixel_address];

    always @* begin
        memory_address=fade_address;
        memory_write=1'b0;
        memory_data=0;
        case (state)
            CLEAR: begin
                memory_address=clear_address;
                memory_write=!reset;
            end
            DRAW_READ, DRAW_BLEND, DRAW_WRITE: begin
                memory_address=in_bounds ? py*WIDTH+px : 0;
                memory_write=state==DRAW_WRITE && in_bounds;
                memory_data=draw_data;
            end
            FADE_WRITE: begin
                memory_write=1'b1;
                memory_data=fade_data;
            end
            default: begin end
        endcase
    end
    always @(posedge clk) begin
        memory_read <= framebuffer[memory_address];
        if (memory_write) framebuffer[memory_address] <= memory_data;
        // Break the wide BRAM read/multiplexer/blend/write route. Drawing
        // waits for the requested pixel; fading prefetched its address in
        // IDLE, so it retains the original three clocks per background pixel.
        if (state==DRAW_BLEND)
            draw_data <= memory_read>stroke_intensity ? memory_read : stroke_intensity;
        if (state==FADE_READ)
            fade_data <= memory_read==0 ? 8'd0 :
                         memory_read-(memory_read>=4 ? memory_read>>2 : 8'd1);
    end

    always @(posedge clk) begin : engine
        reg signed [17:0] dx, dy, twice_error;
        reg signed [17:0] updated_error;
        if (reset) begin
            state<=CLEAR; initialized<=1'b0;
            write_pointer<=0; read_pointer<=0; queued<=0;
            clear_address<=0; fade_address<=0; fade_phase<=0; fade_debt<=0;
            drawn_lines<=0; overflow_count<=0;
            px<=0; py<=0; end_x<=0; end_y<=0;
            delta_x<=0; delta_y<=0; error_term<=0;
            step_x<=0; step_y<=0; stroke_intensity<=0;
        end else begin
            if (line_valid && !line_ready) overflow_count<=overflow_count+1'b1;
            if (push) begin
                queue[write_pointer]<={line_x0,line_y0,line_x1,line_y1,line_intensity};
                write_pointer<=write_pointer+1'b1;
            end
            if (pop) read_pointer<=read_pointer+1'b1;
            case ({push,pop})
                2'b10: queued<=queued+1'b1;
                2'b01: queued<=queued-1'b1;
                default: begin end
            endcase
            if (initialized && !pause) begin
                fade_phase<=fade_due ? fade_phase+PIXELS*DECAY_HZ-CLOCK_HZ :
                                      fade_phase+PIXELS*DECAY_HZ;
            end
            case ({fade_due && initialized,fade_complete})
                2'b10: if (fade_debt!=16'hffff) fade_debt<=fade_debt+1'b1;
                2'b01: fade_debt<=fade_debt-1'b1;
                default: begin end
            endcase
            case (state)
                CLEAR: begin
                    if (clear_address==PIXELS-1) begin
                        state<=IDLE; initialized<=1'b1;
                    end else clear_address<=clear_address+1'b1;
                end
                IDLE: begin
                    if (take_fade) state<=FADE_READ;
                    else if (pop) begin
                        // Reject segments wholly outside the same edge. Inputs
                        // are viewport-clipped by the machine/ARM adapter.
                        if (next_line[7:0]!=0 &&
                            !((next_x0<0 && next_x1<0) ||
                              (next_y0<0 && next_y1<0) ||
                              (next_x0>=WIDTH && next_x1>=WIDTH) ||
                              (next_y0>=HEIGHT && next_y1>=HEIGHT))) begin
                            px<=next_x0; py<=next_y0; end_x<=next_x1; end_y<=next_y1;
                            dx=next_x1>=next_x0 ? $signed(next_x1)-$signed(next_x0) :
                                                         $signed(next_x0)-$signed(next_x1);
                            dy=next_y1>=next_y0 ? $signed(next_y1)-$signed(next_y0) :
                                                         $signed(next_y0)-$signed(next_y1);
                            delta_x<=dx; delta_y<=-dy; error_term<=dx-dy;
                            step_x<=next_x1>=next_x0 ? 2'sd1 : -2'sd1;
                            step_y<=next_y1>=next_y0 ? 2'sd1 : -2'sd1;
                            stroke_intensity<=next_line[7:0]; state<=DRAW_READ;
                        end
                    end
                end
                DRAW_READ: state<=DRAW_BLEND;
                DRAW_BLEND: state<=DRAW_WRITE;
                DRAW_WRITE: begin
                    if (px==end_x && py==end_y) begin
                        state<=IDLE; drawn_lines<=drawn_lines+1'b1;
                    end else begin
                        twice_error=error_term*2; updated_error=error_term;
                        if (twice_error>=delta_y) begin
                            updated_error=updated_error+delta_y; px<=px+step_x;
                        end
                        if (twice_error<=delta_x) begin
                            updated_error=updated_error+delta_x; py<=py+step_y;
                        end
                        error_term<=updated_error; state<=DRAW_READ;
                    end
                end
                FADE_READ: state<=FADE_WRITE;
                FADE_WRITE: begin
                    fade_address<=fade_address==PIXELS-1 ? 0 : fade_address+1'b1;
                    state<=IDLE;
                end
                default: state<=CLEAR;
            endcase
        end
    end
`ifndef SYNTHESIS
    initial begin
        if (PIXELS*DECAY_HZ>=CLOCK_HZ/3)
            $fatal(1,"Renderer requires bandwidth for fade reads/writes and vectors");
        if ((1<<QW)!=QUEUE_DEPTH) $fatal(1,"QUEUE_DEPTH must be a power of two");
    end
`endif
endmodule

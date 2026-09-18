// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// Vectrex-only 64-byte dual-QPI engine. Physical I/O is derived from
// hdl/board/multitini_psram_driver.sv; its shared behavior is unchanged.
// LY68L6400 rev1.2 sections10--13: each chip uses aligned32-byte wrapped
// bursts, EB+24-bit address+6 dummy clocks, followed by64 data clocks.
// A carries even logical bytes, B odd bytes; a logical64B boundary maps to
// a physical32B boundary. Thus no burst crosses either a wrap or1KiB page.
// At96MHz CE stays low <0.9us (<8us tCEM), with >50ns high between commands.
module multitini_vectrex_psram_burst (
    input wire clk, capture_clk, resetn,
    input wire request_valid, request_write,
    input wire [23:0] request_addr,
    input wire [511:0] request_wdata,
    output wire request_ready,
    output reg response_done, read_word_valid,
    output reg [4:0] read_word_index,
    output reg [15:0] read_word,
    output reg initialized, fault,
    inout wire [3:0] psram_a_io, psram_b_io,
    output reg psram_ce_n,
    output wire psram_clk
);
    reg [95:0] ce_tape, clock_tape, output_tape, capture_tape;
    reg [287:0] write_a_tape, write_b_tape;
    reg [3:0] psram_t, psram_a_o, psram_b_o;
    reg clock_enable;
    wire [3:0] psram_a_i, psram_b_i;
    reg busy, quad, reading, external_operation;
    reg previous_ce, previous_clock, previous_capture;
    reg [2:0] rest;
    reg [2:0] init_phase;
    reg [14:0] init_wait;
    reg init_outstanding;
    reg operation_done;
    reg capture_half;
    reg [3:0] high_a, high_b;
    reg [4:0] capture_index;
    // previous_ce closes the one-cycle gap before the CE-high detector loads
    // rest. Without it a new command could be accepted at that same edge.
    wire driver_ready=!busy && rest==0 && !operation_done && ce_tape[95] && previous_ce;
    wire init_issue=!initialized && init_wait==0 && !init_outstanding;
    wire launch=driver_ready && (init_issue || (initialized && request_valid));
    wire [7:0] launch_command=initialized ? (request_write ? 8'h02 : 8'heb) :
        init_phase==0 || init_phase==2 ? 8'h66 :
        init_phase==1 || init_phase==3 ? 8'h99 : init_phase==4 ? 8'h35 : 8'hc0;
    wire launch_quad=initialized || init_phase<2 || init_phase==5;
    wire [23:0] physical_address={1'b0,request_addr[23:1]};
    assign request_ready=resetn && initialized && driver_ready;
    wire capture_drop=previous_capture && !capture_tape[95];
    wire clock_drop=previous_clock && !clock_tape[95];

    // A second output of the same96MHz MMCM has phase279 degrees. Its input
    // FFs sample13.281ns after the preceding logical SCK falling edge, then
    // the main clock consumes that nibble2.344ns later. Both crossings are
    // synchronously timed, including FPGA clock-output and input-buffer delay.
    // No IDELAY or unused opposite-edge capture arc is present.
    (* IOB="TRUE" *) reg [3:0] sample_a, sample_b;
    always @(posedge capture_clk) begin
        sample_a<=psram_a_i;
        sample_b<=psram_b_i;
    end
    genvar pin_index;
    generate for(pin_index=0;pin_index<4;pin_index=pin_index+1) begin:g_phy
        IOBUF a_iobuf(.I(psram_a_o[pin_index]),.O(psram_a_i[pin_index]),.T(psram_t[pin_index]),.IO(psram_a_io[pin_index]));
        IOBUF b_iobuf(.I(psram_b_o[pin_index]),.O(psram_b_i[pin_index]),.T(psram_t[pin_index]),.IO(psram_b_io[pin_index]));
    end endgenerate
    ODDR #(.DDR_CLK_EDGE("SAME_EDGE"),.INIT(0),.SRTYPE("ASYNC")) psram_clk_oddr_i(
        .Q(psram_clk),.C(clk),.CE(1'b1),.D1(1'b1),.D2(1'b0),.R(!clock_enable),.S(1'b0));
    always @(negedge clk) begin
        // Asynchronous ODDR reset may change only while the clock is low;
        // driving it directly from a posedge tape truncates the final pulse.
        clock_enable<=resetn && clock_tape[95];
        psram_ce_n<=!resetn || ce_tape[95];
        psram_t<=quad ? {4{!output_tape[95]}} : {3'b111,!output_tape[95]};
        psram_a_o<=quad ? write_a_tape[287:284] : {3'd0,write_a_tape[287]};
        psram_b_o<=quad ? write_b_tape[287:284] : {3'd0,write_b_tape[287]};
    end

    integer byte_index;
    always @(posedge clk) begin
        operation_done<=0;response_done<=0;read_word_valid<=0;
        if (!resetn) begin
            ce_tape<={96{1'b1}};clock_tape<=0;output_tape<=0;capture_tape<=0;
            write_a_tape<=0;write_b_tape<=0;busy<=0;quad<=0;reading<=0;
            previous_ce<=1;previous_clock<=0;previous_capture<=0;rest<=0;
            initialized<=0;fault<=0;init_phase<=0;init_wait<=15000;init_outstanding<=0;
            capture_half<=0;capture_index<=0;high_a<=0;high_b<=0;
            read_word<=0;read_word_index<=0;external_operation<=0;
        end else begin
            ce_tape<={ce_tape[94:0],1'b1};clock_tape<={clock_tape[94:0],1'b0};
            output_tape<={output_tape[94:0],1'b0};capture_tape<={capture_tape[94:0],1'b0};
            if(output_tape[95]) begin
                write_a_tape<=quad ? {write_a_tape[283:0],4'd0} : {write_a_tape[286:0],1'b0};
                write_b_tape<=quad ? {write_b_tape[283:0],4'd0} : {write_b_tape[286:0],1'b0};
            end
            previous_ce<=ce_tape[95];previous_clock<=clock_tape[95];previous_capture<=capture_tape[95];
            if(!previous_ce && ce_tape[95]) rest<=6;
            else if(rest!=0) rest<=rest-1'b1;
            if(init_wait!=0) init_wait<=init_wait-1'b1;
            if(operation_done && init_outstanding) begin
                init_outstanding<=0;
                if(init_phase==5) initialized<=1;
                else begin init_phase<=init_phase+1'b1;
                    if(init_phase==1 || init_phase==3) init_wait<=15000;
                end
            end
            if(capture_tape[95]) begin
                capture_half<=!capture_half;
                if(!capture_half) begin high_a<=sample_a;high_b<=sample_b;end
                else begin
                    read_word<={high_b,sample_b,high_a,sample_a};
                    read_word_index<=capture_index;capture_index<=capture_index+1'b1;
                    read_word_valid<=1;
                end
            end
            if(busy && (reading ? capture_drop : clock_drop)) begin
                busy<=0;operation_done<=1;
                if(external_operation) response_done<=1;
            end
            if(launch) begin
                if(initialized && request_addr[5:0]!=0) begin
                    fault<=1;response_done<=1;
                end else begin
                    busy<=1;quad<=launch_quad;reading<=launch_command==8'heb;
                    external_operation<=initialized;
                    if(!initialized) init_outstanding<=1;
                    capture_half<=0;capture_index<=0;
                    write_a_tape<=0;write_b_tape<=0;capture_tape<=0;
                    if(initialized) begin
                        write_a_tape[287:256]<={launch_command,physical_address};
                        write_b_tape[287:256]<={launch_command,physical_address};
                        if(request_write) begin
                            for(byte_index=0;byte_index<32;byte_index=byte_index+1) begin
                                write_a_tape[255-byte_index*8 -:8]<=request_wdata[byte_index*16 +:8];
                                write_b_tape[255-byte_index*8 -:8]<=request_wdata[byte_index*16+8 +:8];
                            end
                            ce_tape<={{74{1'b0}},{22{1'b1}}};
                            clock_tape<={{72{1'b1}},{24{1'b0}}};
                            output_tape<={{72{1'b1}},{24{1'b0}}};
                        end else begin
                            ce_tape<={{81{1'b0}},{15{1'b1}}};
                            clock_tape<={{78{1'b1}},{18{1'b0}}};
                            output_tape<={{8{1'b1}},{88{1'b0}}};
                            capture_tape<={{15{1'b0}},{64{1'b1}},{17{1'b0}}};
                        end
                    end else begin
                        write_a_tape[287:280]<=launch_command;
                        write_b_tape[287:280]<=launch_command;
                        // tCHD is20ns after the last rising clock:2.5 cycles.
                        ce_tape<=launch_quad ? {{4{1'b0}},{92{1'b1}}} : {{10{1'b0}},{86{1'b1}}};
                        clock_tape<=launch_quad ? {{2{1'b1}},{94{1'b0}}} : {{8{1'b1}},{88{1'b0}}};
                        output_tape<=launch_quad ? {{2{1'b1}},{94{1'b0}}} : {{8{1'b1}},{88{1'b0}}};
                    end
                end
            end
        end
    end
endmodule

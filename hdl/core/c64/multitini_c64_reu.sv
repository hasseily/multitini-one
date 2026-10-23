// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// MOS 8726 register/transfer behavior follows the pinned MAME reference.
// Reference: mamedev/mame 122719e8d13e2fdb7dd33886455484207de59fc8,
// src/devices/machine/mos8726.cpp, BSD-3-Clause, copyright-holders: Curt Coder.
// Original notices and license are retained in third_party/mame_reference/.
// ADDRESS_BITS=19 is the 1750 reference profile; 24 extends its bank register
// and address wrap to 16 MiB. Storage latency does not stop machine clocks.
module multitini_c64_reu #(
    parameter integer ADDRESS_BITS = 24
) (
    input  logic        clk,
    input  logic        reset,
    input  logic        enable,
    input  logic        io_valid,
    input  logic [4:0]  io_addr,
    input  logic        io_write,
    input  logic [7:0]  io_wdata,
    output logic [7:0]  io_rdata,
    input  logic        ff00_write,

    output logic        dma_req,
    output logic        dma_valid,
    output logic [15:0] dma_addr,
    output logic [7:0]  dma_wdata,
    output logic        dma_write,
    input  logic        dma_ack,
    input  logic [7:0]  dma_rdata,

    output logic [23:0] memory_addr,
    output logic        memory_write,
    output logic [63:0] memory_wdata,
    output logic [7:0]  memory_wstrb,
    output logic        memory_valid,
    output logic [23:0] memory_hint_addr,
    output logic        memory_hint_read,
    input  logic        memory_ready,
    input  logic [63:0] memory_rdata,
    input  logic        memory_rvalid,
    input  logic        memory_fault,
    output logic        irq,
    output logic        busy,
    output logic        fault
);
    localparam logic [23:0] ADDRESS_MASK =
        ADDRESS_BITS == 19 ? 24'h07ffff : 24'hffffff;
    localparam logic [7:0] BANK_UNUSED =
        ADDRESS_BITS == 19 ? 8'hf8 : 8'h00;
    typedef enum logic [3:0] {
        IDLE, BEGIN_BYTE, C64_READ, RAM_READ_REQUEST, RAM_READ_WAIT,
        C64_WRITE, RAM_WRITE_REQUEST, RAM_WRITE_WAIT, FINISH_BYTE,
        TAIL_READ_REQUEST, TAIL_READ_WAIT, COMPLETE
    } state_t;
    state_t state = IDLE;
    logic [7:0] status, command, interrupt_mask, address_control;
    logic [15:0] c64_address, c64_shadow, length, length_shadow;
    logic [23:0] reu_address, reu_shadow;
    logic [7:0] c64_byte, reu_byte;
    logic verify_fault;
    logic active;
    // This transport flag is deliberately drained across soft reset/disable:
    // an old response must not complete a new transaction after re-enabling.
    logic memory_pending = 1'b0;
    logic run, memory_accept, memory_response;
    logic [7:0] response_byte;

    assign run = enable && !reset && !fault && !memory_fault;
    assign dma_req = active && run;
    assign dma_valid = run && (state == C64_READ || state == C64_WRITE);
    assign dma_addr = c64_address;
    assign dma_write = state == C64_WRITE;
    assign dma_wdata = reu_byte;
    assign irq = status[7] && run;
    assign busy = active || memory_pending;

    assign memory_addr = {reu_address[23:3], 3'b000};
    // Prepare the first line while software programs the registers, and
    // stream the following line ahead of fetch/swap/verify. Stash needs no
    // speculative reads while active; writes are combined by the DDR cache.
    assign memory_hint_addr = reu_address;
    assign memory_hint_read = run && (!active || command[1:0] != 0);
    assign memory_write = state == RAM_WRITE_REQUEST;
    assign memory_wdata = {56'd0, c64_byte} << (reu_address[2:0] * 8);
    assign memory_wstrb = memory_write ? (8'b1 << reu_address[2:0]) : 8'd0;
    assign memory_valid = run && !memory_pending &&
        (state == RAM_READ_REQUEST || state == RAM_WRITE_REQUEST ||
         state == TAIL_READ_REQUEST);
    assign memory_accept = memory_valid && memory_ready;
    assign memory_response = memory_rvalid && (memory_pending || memory_accept);
    assign response_byte = memory_rdata[reu_address[2:0] * 8 +: 8];

    always_comb begin
        io_rdata = 8'hff;
        if (enable && !active) begin
            case (io_addr)
                0: io_rdata = status | 8'h10;
                1: io_rdata = command;
                2: io_rdata = c64_address[7:0];
                3: io_rdata = c64_address[15:8];
                4: io_rdata = reu_address[7:0];
                5: io_rdata = reu_address[15:8];
                6: io_rdata = reu_address[23:16] | BANK_UNUSED;
                7: io_rdata = length[7:0];
                8: io_rdata = length[15:8];
                9: io_rdata = interrupt_mask | 8'h1f;
               10: io_rdata = address_control | 8'h3f;
                default: ;
            endcase
        end
    end

    always_ff @(posedge clk) begin
        if (memory_fault) memory_pending <= 1'b0;
        else if (memory_accept) memory_pending <= !memory_rvalid;
        else if (memory_response) memory_pending <= 1'b0;

        if (reset || !enable) begin
            state <= IDLE;
            status <= 8'd0;
            command <= 8'h10;
            c64_address <= 16'd0;
            c64_shadow <= 16'd0;
            reu_address <= 24'd0;
            reu_shadow <= 24'd0;
            length <= 16'hffff;
            length_shadow <= 16'hffff;
            interrupt_mask <= 8'd0;
            address_control <= 8'd0;
            active <= 1'b0;
            c64_byte <= 8'd0;
            reu_byte <= 8'd0;
            verify_fault <= 1'b0;
            fault <= 1'b0;
        end else if (memory_fault) begin
            state <= IDLE;
            active <= 1'b0;
            fault <= 1'b1;
        end else if (!fault) begin
            // IO2 is decoded in the machine wrapper. io_valid is a single
            // accepted CPU transaction, including $20 register mirroring.
            if (io_valid && !active) begin
                if (!io_write) begin
                    if (io_addr == 0) status <= 8'd0;
                end else begin
                    case (io_addr)
                        1: begin
                            command <= io_wdata;
                            if ((io_wdata & 8'h90) == 8'h90) begin
                                command <= (io_wdata & 8'h7f) | 8'h10;
                                active <= 1'b1;
                                state <= BEGIN_BYTE;
                            end
                        end
                        2: begin
                            c64_address <= {c64_shadow[15:8], io_wdata};
                            c64_shadow[7:0] <= io_wdata;
                        end
                        3: begin
                            c64_address <= {io_wdata, c64_shadow[7:0]};
                            c64_shadow[15:8] <= io_wdata;
                        end
                        4: begin
                            reu_address <= {reu_shadow[23:8], io_wdata};
                            reu_shadow[7:0] <= io_wdata;
                        end
                        5: begin
                            reu_address <= {reu_shadow[23:16], io_wdata, reu_shadow[7:0]};
                            reu_shadow[15:8] <= io_wdata;
                        end
                        6: begin
                            reu_address <= {io_wdata & ADDRESS_MASK[23:16], reu_shadow[15:0]};
                            reu_shadow[23:16] <= io_wdata & ADDRESS_MASK[23:16];
                        end
                        7: begin
                            length <= {length_shadow[15:8], io_wdata};
                            length_shadow[7:0] <= io_wdata;
                        end
                        8: begin
                            length <= {io_wdata, length_shadow[7:0]};
                            length_shadow[15:8] <= io_wdata;
                        end
                        9: interrupt_mask <= io_wdata & 8'he0;
                       10: address_control <= io_wdata & 8'hc0;
                        default: ;
                    endcase
                end
            end

            if (ff00_write && !active && (command & 8'h90) == 8'h80) begin
                command <= (command & 8'h7f) | 8'h10;
                active <= 1'b1;
                state <= BEGIN_BYTE;
            end

            case (state)
                IDLE: ;
                BEGIN_BYTE: if (!memory_pending) begin
                    verify_fault <= 1'b0;
                    if (command[1:0] == 1) state <= RAM_READ_REQUEST;
                    else state <= C64_READ;
                end
                C64_READ: if (dma_ack) begin
                    c64_byte <= dma_rdata;
                    if (command[1:0] == 0) state <= RAM_WRITE_REQUEST;
                    else state <= RAM_READ_REQUEST;
                end
                RAM_READ_REQUEST: if (memory_accept) begin
                    if (memory_rvalid) begin
                        reu_byte <= response_byte;
                        verify_fault <= command[1:0] == 3 && c64_byte != response_byte;
                        if (command[1:0] == 3) state <= FINISH_BYTE;
                        else state <= C64_WRITE;
                    end else state <= RAM_READ_WAIT;
                end
                RAM_READ_WAIT: if (memory_response) begin
                    reu_byte <= response_byte;
                    verify_fault <= command[1:0] == 3 && c64_byte != response_byte;
                    if (command[1:0] == 3) state <= FINISH_BYTE;
                    else state <= C64_WRITE;
                end
                C64_WRITE: if (dma_ack) begin
                    if (command[1:0] == 2) state <= RAM_WRITE_REQUEST;
                    else state <= FINISH_BYTE;
                end
                RAM_WRITE_REQUEST: if (memory_accept) begin
                    if (memory_rvalid) state <= FINISH_BYTE;
                    else state <= RAM_WRITE_WAIT;
                end
                RAM_WRITE_WAIT: if (memory_response) state <= FINISH_BYTE;
                FINISH_BYTE: begin
                    if (!address_control[7]) c64_address <= c64_address + 16'd1;
                    if (!address_control[6]) reu_address <= (reu_address + 24'd1) & ADDRESS_MASK;
                    // A terminal transfer leaves length=1. Length=0 means
                    // 65536 bytes; an early verify failure decrements it.
                    if (length == 1) status[6] <= 1'b1;
                    else length <= length - 16'd1;
                    if (verify_fault) status[5] <= 1'b1;
                    if (length == 1 || verify_fault) begin
                        if (command[1:0] == 1) state <= TAIL_READ_REQUEST;
                        else state <= COMPLETE;
                    end
                    else state <= BEGIN_BYTE;
                end
                // MAME fetch performs one final REU read at the incremented
                // address before autoload. Retain that observable access.
                TAIL_READ_REQUEST: if (memory_accept) begin
                    if (memory_rvalid) state <= COMPLETE;
                    else state <= TAIL_READ_WAIT;
                end
                TAIL_READ_WAIT: if (memory_response) state <= COMPLETE;
                COMPLETE: begin
                    if (command[5]) begin
                        c64_address <= c64_shadow;
                        reu_address <= reu_shadow;
                        length <= length_shadow;
                    end
                    if (interrupt_mask[7] && |(interrupt_mask[6:5] & status[6:5]))
                        status[7] <= 1'b1;
                    active <= 1'b0;
                    state <= IDLE;
                end
                default: state <= IDLE;
            endcase
        end
    end
endmodule

// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// Keep the selected PSRAM client through acceptance and response. Soft machine
// reset does not reset this arbiter: a late completion still belongs to the
// client that issued it. The adapter excludes CRT and REU address ownership.
module multitini_c64_memory_arbiter (
    input logic clk, reset,
    input logic [23:0] cart_addr, reu_addr,
    input logic cart_write, reu_write,
    input logic [63:0] cart_wdata, reu_wdata,
    input logic [7:0] cart_wstrb, reu_wstrb,
    input logic cart_valid, reu_valid,
    output logic cart_ready, reu_ready,
    output logic cart_rvalid, reu_rvalid,
    output logic [23:0] memory_addr,
    output logic memory_write,
    output logic [63:0] memory_wdata,
    output logic [7:0] memory_wstrb,
    output logic memory_valid,
    input logic memory_ready, memory_rvalid, memory_fault
);
    typedef enum logic [2:0] {IDLE, CART_ISSUE, REU_ISSUE, CART_WAIT, REU_WAIT} state_t;
    state_t state;
    wire cart_selected = state == CART_ISSUE || state == CART_WAIT;
    wire issuing = state == CART_ISSUE || state == REU_ISSUE;
    wire accepted = memory_valid && memory_ready;
    wire responding = !reset && !memory_fault && memory_rvalid &&
                      (accepted || state == CART_WAIT || state == REU_WAIT);
    assign memory_addr = cart_selected ? cart_addr : reu_addr;
    assign memory_write = cart_selected ? cart_write : reu_write;
    assign memory_wdata = cart_selected ? cart_wdata : reu_wdata;
    assign memory_wstrb = cart_selected ? cart_wstrb : reu_wstrb;
    assign memory_valid = !reset && !memory_fault && issuing && (cart_selected ? cart_valid : reu_valid);
    assign cart_ready = !reset && !memory_fault && state == CART_ISSUE && memory_ready;
    assign reu_ready = !reset && !memory_fault && state == REU_ISSUE && memory_ready;
    assign cart_rvalid = responding && cart_selected;
    assign reu_rvalid = responding && !cart_selected;
    always_ff @(posedge clk) begin
        if (reset || memory_fault) state <= IDLE;
        else case (state)
            IDLE: if (cart_valid) state <= CART_ISSUE;
                  else if (reu_valid) state <= REU_ISSUE;
            CART_ISSUE: if (!cart_valid || (accepted && memory_rvalid)) state <= IDLE;
                        else if (accepted) state <= CART_WAIT;
            REU_ISSUE: if (!reu_valid || (accepted && memory_rvalid)) state <= IDLE;
                       else if (accepted) state <= REU_WAIT;
            CART_WAIT, REU_WAIT: if (memory_rvalid) state <= IDLE;
            default: state <= IDLE;
        endcase
    end
endmodule

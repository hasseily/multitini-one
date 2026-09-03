// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// Converts the common byte-strobed 64-bit line service into Appletini's QPI
// driver commands. Partial writes use a read/merge/write sequence.
module multitini_psram_line_service (
    input  logic         clk,
    input  logic         resetn,
    output logic         initialized,
    output logic         fault,

    input  logic [23:0]  request_addr,
    input  logic         request_write,
    input  logic [63:0]  request_wdata,
    input  logic [7:0]   request_wstrb,
    input  logic         request_valid,
    output logic         request_ready,
    output logic [63:0]  response_rdata,
    output logic         response_valid,

    output logic         command_valid,
    input  logic         command_ready,
    output logic [7:0]   command,
    output logic [23:0]  command_addr,
    output logic [63:0]  command_wdata,
    input  logic         driver_response_valid,
    input  logic [63:0]  driver_response_rdata
);
    localparam logic [7:0] COMMAND_ENTER_QPI = 8'h35;
    localparam logic [7:0] COMMAND_TOGGLE_WRAP = 8'hC0;
    localparam logic [7:0] COMMAND_WRITE = 8'h02;
    localparam logic [7:0] COMMAND_READ = 8'hEB;

    typedef enum logic [3:0] {
        STATE_INIT_QPI_ISSUE,
        STATE_INIT_QPI_WAIT,
        STATE_INIT_WRAP_ISSUE,
        STATE_INIT_WRAP_WAIT,
        STATE_IDLE,
        STATE_READ_ISSUE,
        STATE_READ_WAIT,
        STATE_WRITE_ISSUE,
        STATE_WRITE_WAIT
    } state_t;

    state_t state;
    logic [23:0] address_hold;
    logic [63:0] write_hold;
    logic [7:0] strobe_hold;
    logic partial_write;

    // The two x4 PSRAMs run in parallel. Each command transfers four
    // consecutive bytes per device: the A device carries logical bytes 0-3
    // and the B device carries logical bytes 4-7. Pack each aligned logical
    // 8-byte line into four physical byte addresses so the combined pair
    // exposes the full 16 MiB logical space. Passing the logical address
    // through unchanged would waste every other four-byte region and place
    // logical cartridge RAM at 0x800000, beyond each device's A[22:0] range.
    function automatic logic [23:0] physical_address(
        input logic [23:0] logical_address
    );
        physical_address = {1'b0, logical_address[23:3], 2'b00};
    endfunction

    function automatic logic [63:0] merge_bytes(
        input logic [63:0] old_data,
        input logic [63:0] new_data,
        input logic [7:0] strobes
    );
        integer index;
        begin
            merge_bytes = old_data;
            for (index = 0; index < 8; index = index + 1)
                if (strobes[index])
                    merge_bytes[index * 8 +: 8] = new_data[index * 8 +: 8];
        end
    endfunction

    assign request_ready = initialized && state == STATE_IDLE;

    always_comb begin
        command_valid = 1'b0;
        command = 8'd0;
        command_addr = physical_address(address_hold);
        command_wdata = write_hold;
        unique case (state)
        STATE_INIT_QPI_ISSUE: begin
            command_valid = 1'b1;
            command = COMMAND_ENTER_QPI;
            command_addr = 24'd0;
            command_wdata = 64'd0;
        end
        STATE_INIT_WRAP_ISSUE: begin
            command_valid = 1'b1;
            command = COMMAND_TOGGLE_WRAP;
            command_addr = 24'd0;
            command_wdata = 64'd0;
        end
        STATE_READ_ISSUE: begin
            command_valid = 1'b1;
            command = COMMAND_READ;
        end
        STATE_WRITE_ISSUE: begin
            command_valid = 1'b1;
            command = COMMAND_WRITE;
        end
        default: ;
        endcase
    end

    always_ff @(posedge clk) begin
        response_valid <= 1'b0;
        if (!resetn) begin
            state <= STATE_INIT_QPI_ISSUE;
            initialized <= 1'b0;
            fault <= 1'b0;
            address_hold <= 24'd0;
            write_hold <= 64'd0;
            strobe_hold <= 8'd0;
            partial_write <= 1'b0;
            response_rdata <= 64'd0;
        end else begin
            unique case (state)
            STATE_INIT_QPI_ISSUE:
                if (command_valid && command_ready)
                    state <= STATE_INIT_QPI_WAIT;
            STATE_INIT_QPI_WAIT:
                if (driver_response_valid)
                    state <= STATE_INIT_WRAP_ISSUE;
            STATE_INIT_WRAP_ISSUE:
                if (command_valid && command_ready)
                    state <= STATE_INIT_WRAP_WAIT;
            STATE_INIT_WRAP_WAIT:
                if (driver_response_valid) begin
                    initialized <= 1'b1;
                    state <= STATE_IDLE;
                end
            STATE_IDLE: begin
                if (request_valid && request_ready) begin
                    address_hold <= request_addr;
                    write_hold <= request_wdata;
                    strobe_hold <= request_wstrb;
                    partial_write <= request_write && request_wstrb != 8'hff;
                    if (request_addr[2:0] != 3'b000 ||
                        (request_write && request_wstrb == 8'h00)) begin
                        fault <= 1'b1;
                        response_rdata <= 64'd0;
                        response_valid <= 1'b1;
                    end else if (!request_write || request_wstrb != 8'hff) begin
                        state <= STATE_READ_ISSUE;
                    end else begin
                        state <= STATE_WRITE_ISSUE;
                    end
                end
            end
            STATE_READ_ISSUE:
                if (command_valid && command_ready)
                    state <= STATE_READ_WAIT;
            STATE_READ_WAIT:
                if (driver_response_valid) begin
                    if (partial_write) begin
                        write_hold <= merge_bytes(driver_response_rdata,
                                                  write_hold, strobe_hold);
                        state <= STATE_WRITE_ISSUE;
                    end else begin
                        response_rdata <= driver_response_rdata;
                        response_valid <= 1'b1;
                        state <= STATE_IDLE;
                    end
                end
            STATE_WRITE_ISSUE:
                if (command_valid && command_ready)
                    state <= STATE_WRITE_WAIT;
            STATE_WRITE_WAIT:
                if (driver_response_valid) begin
                    response_rdata <= write_hold;
                    response_valid <= 1'b1;
                    state <= STATE_IDLE;
                end
            default: state <= STATE_INIT_QPI_ISSUE;
            endcase
        end
    end
endmodule

// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// Destructive line-oriented PSRAM test sequencer. This block owns only the
// ready/valid command interface; the PHY, delay calibration, and pin timing
// remain separate. line_count is the number of 8-byte lines starting at zero.
module multitini_psram_bist (
    input  logic         clk,
    input  logic         resetn,
    input  logic         start,
    input  logic [20:0]  line_count,
    input  logic [31:0]  seed,

    output logic         busy,
    output logic         done,
    output logic         passed,
    output logic [31:0]  error_count,
    output logic [23:0]  first_bad_addr,
    output logic [63:0]  first_bad_expected,
    output logic [63:0]  first_bad_actual,
    output logic [31:0]  max_read_latency,
    output logic [31:0]  max_write_latency,
    output logic [31:0]  max_turnaround,

    output logic         command_valid,
    input  logic         command_ready,
    output logic [7:0]   command,
    output logic [23:0]  command_addr,
    output logic [63:0]  command_wdata,
    input  logic         response_valid,
    input  logic [63:0]  response_rdata
);
    localparam logic [7:0] COMMAND_ENTER_QPI = 8'h35;
    localparam logic [7:0] COMMAND_TOGGLE_WRAP = 8'hC0;
    localparam logic [7:0] COMMAND_WRITE = 8'h02;
    localparam logic [7:0] COMMAND_READ = 8'hEB;

    typedef enum logic [3:0] {
        STATE_IDLE,
        STATE_INIT_QPI_ISSUE,
        STATE_INIT_QPI_WAIT,
        STATE_INIT_WRAP_ISSUE,
        STATE_INIT_WRAP_WAIT,
        STATE_WRITE_ISSUE,
        STATE_WRITE_WAIT,
        STATE_READ_ISSUE,
        STATE_READ_WAIT
    } state_t;

    state_t state;
    logic [20:0] line_index;
    logic [20:0] requested_lines;
    logic [31:0] active_seed;
    logic [31:0] latency_count;
    logic [31:0] turnaround_count;
    logic        measuring_turnaround;

    function automatic logic [63:0] pattern(
        input logic [20:0] index,
        input logic [31:0] pattern_seed
    );
        logic [31:0] mixed;
        begin
            mixed = pattern_seed ^ {8'hA5, index, 3'b000};
            pattern = {mixed, ~mixed};
        end
    endfunction

    function automatic logic [31:0] increment_saturating(
        input logic [31:0] value
    );
        increment_saturating = (&value) ? value : value + 32'd1;
    endfunction

    always_comb begin
        command_valid = 1'b0;
        command = 8'h00;
        command_addr = {line_index, 3'b000};
        command_wdata = pattern(line_index, active_seed);
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
        STATE_WRITE_ISSUE: begin
            command_valid = 1'b1;
            command = COMMAND_WRITE;
        end
        STATE_READ_ISSUE: begin
            command_valid = 1'b1;
            command = COMMAND_READ;
        end
        default: ;
        endcase
    end

    always_ff @(posedge clk) begin
        if (!resetn) begin
            state <= STATE_IDLE;
            line_index <= 21'd0;
            requested_lines <= 21'd0;
            active_seed <= 32'd0;
            latency_count <= 32'd0;
            turnaround_count <= 32'd0;
            measuring_turnaround <= 1'b0;
            busy <= 1'b0;
            done <= 1'b0;
            passed <= 1'b0;
            error_count <= 32'd0;
            first_bad_addr <= 24'd0;
            first_bad_expected <= 64'd0;
            first_bad_actual <= 64'd0;
            max_read_latency <= 32'd0;
            max_write_latency <= 32'd0;
            max_turnaround <= 32'd0;
        end else begin
            done <= 1'b0;

            if (measuring_turnaround && !command_ready) begin
                turnaround_count <= increment_saturating(turnaround_count);
            end
            if (command_valid && command_ready && measuring_turnaround) begin
                if (turnaround_count > max_turnaround)
                    max_turnaround <= turnaround_count;
                turnaround_count <= 32'd0;
                measuring_turnaround <= 1'b0;
            end

            unique case (state)
            STATE_IDLE: begin
                busy <= 1'b0;
                if (start) begin
                    requested_lines <= line_count;
                    active_seed <= seed;
                    line_index <= 21'd0;
                    error_count <= 32'd0;
                    first_bad_addr <= 24'd0;
                    first_bad_expected <= 64'd0;
                    first_bad_actual <= 64'd0;
                    max_read_latency <= 32'd0;
                    max_write_latency <= 32'd0;
                    max_turnaround <= 32'd0;
                    turnaround_count <= 32'd0;
                    measuring_turnaround <= 1'b0;
                    passed <= 1'b0;
                    busy <= 1'b1;
                    if (line_count == 21'd0) begin
                        done <= 1'b1;
                        busy <= 1'b0;
                    end else begin
                        state <= STATE_INIT_QPI_ISSUE;
                    end
                end
            end
            STATE_INIT_QPI_ISSUE: begin
                if (command_valid && command_ready) begin
                    latency_count <= 32'd1;
                    state <= STATE_INIT_QPI_WAIT;
                end
            end
            STATE_INIT_QPI_WAIT: begin
                if (response_valid) begin
                    measuring_turnaround <= 1'b1;
                    turnaround_count <= 32'd0;
                    state <= STATE_INIT_WRAP_ISSUE;
                end else begin
                    latency_count <= increment_saturating(latency_count);
                end
            end
            STATE_INIT_WRAP_ISSUE: begin
                if (command_valid && command_ready) begin
                    latency_count <= 32'd1;
                    state <= STATE_INIT_WRAP_WAIT;
                end
            end
            STATE_INIT_WRAP_WAIT: begin
                if (response_valid) begin
                    measuring_turnaround <= 1'b1;
                    turnaround_count <= 32'd0;
                    line_index <= 21'd0;
                    state <= STATE_WRITE_ISSUE;
                end else begin
                    latency_count <= increment_saturating(latency_count);
                end
            end
            STATE_WRITE_ISSUE: begin
                if (command_valid && command_ready) begin
                    latency_count <= 32'd1;
                    state <= STATE_WRITE_WAIT;
                end
            end
            STATE_WRITE_WAIT: begin
                if (response_valid) begin
                    if (latency_count > max_write_latency)
                        max_write_latency <= latency_count;
                    measuring_turnaround <= 1'b1;
                    turnaround_count <= 32'd0;
                    if (line_index + 21'd1 == requested_lines) begin
                        line_index <= 21'd0;
                        state <= STATE_READ_ISSUE;
                    end else begin
                        line_index <= line_index + 21'd1;
                        state <= STATE_WRITE_ISSUE;
                    end
                end else begin
                    latency_count <= increment_saturating(latency_count);
                end
            end
            STATE_READ_ISSUE: begin
                if (command_valid && command_ready) begin
                    latency_count <= 32'd1;
                    state <= STATE_READ_WAIT;
                end
            end
            STATE_READ_WAIT: begin
                if (response_valid) begin
                    logic mismatch;
                    mismatch = response_rdata != pattern(line_index, active_seed);
                    if (latency_count > max_read_latency)
                        max_read_latency <= latency_count;
                    if (mismatch) begin
                        error_count <= increment_saturating(error_count);
                        if (error_count == 32'd0) begin
                            first_bad_addr <= {line_index, 3'b000};
                            first_bad_expected <= pattern(line_index, active_seed);
                            first_bad_actual <= response_rdata;
                        end
                    end
                    if (line_index + 21'd1 == requested_lines) begin
                        passed <= (error_count == 32'd0) && !mismatch;
                        busy <= 1'b0;
                        done <= 1'b1;
                        measuring_turnaround <= 1'b0;
                        state <= STATE_IDLE;
                    end else begin
                        measuring_turnaround <= 1'b1;
                        turnaround_count <= 32'd0;
                        line_index <= line_index + 21'd1;
                        state <= STATE_READ_ISSUE;
                    end
                end else begin
                    latency_count <= increment_saturating(latency_count);
                end
            end
            default: state <= STATE_IDLE;
            endcase
        end
    end
endmodule

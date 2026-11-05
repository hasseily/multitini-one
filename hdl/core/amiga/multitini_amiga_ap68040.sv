// SPDX-License-Identifier: GPL-3.0-only
// Adapt the 040 external bus to the native Minimig wrapper. Fast RAM and
// direct Kickstart reads accept aligned longwords through the DDR cache.
module multitini_amiga_ap68040 (
    input wire clk, cpu_clk, cpu_bclk, reset_n,
    input wire [2:0] ipl_n,
    input wire [7:0] fast_top,
    input wire wide_selected, ready,
    input wire [15:0] data_in, data_next,
    output wire [31:0] address,
    output wire [15:0] data_out, data_low,
    output wire [1:0] bus_state,
    output wire read_cycle, uds_n, lds_n, wide_candidate, reset_out_n
);
    typedef enum logic [2:0] {IDLE, REQUEST, WORD_GAP, RESPOND} state_t;
    state_t state;
    wire [31:0] a, d;
    wire [1:0] size, tt;
    wire [2:0] tm;
    wire ts_n, rw_n, a_oe, cpu_reset_out_n;
    logic [31:0] base, read_data, write_data;
    logic native_reset_out_n=1'b1;
    logic [15:0] first_word;
    logic [1:0] size_q;
    logic read_q, second_word, interrupt_ack, transfer_error;
    wire active = state == REQUEST;
    wire reply = state == RESPOND;
    wire long_access = size_q == 2'b00 || size_q == 2'b11;
    wire wide = wide_candidate && wide_selected;

    assign address = base | (second_word ? 32'd2 : 32'd0);
    assign read_cycle = read_q;
    assign bus_state = active ? (read_q ? 2'b10 : 2'b11) : 2'b01;
    assign wide_candidate = active && long_access && !second_word;
    assign uds_n = !active || (size_q == 2'b01 && base[0]);
    assign lds_n = !active || (size_q == 2'b01 && !base[0]);
    assign data_out = address[1] ? write_data[15:0] : write_data[31:16];
    assign data_low = write_data[15:0];
    // RSTO is a bus-clock pulse. Register it at the native boundary
    // before its fan-out into chipset reset and memory request logic.
    always_ff @(posedge clk) native_reset_out_n <= cpu_reset_out_n;
    assign reset_out_n = native_reset_out_n;

    always_ff @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            state <= IDLE;
            base <= 0;
            read_data <= 0;
            write_data <= 0;
            first_word <= 0;
            size_q <= 0;
            read_q <= 1;
            second_word <= 0;
            interrupt_ack <= 0;
            transfer_error <= 0;
        end else case (state)
            IDLE, RESPOND: begin
                state <= IDLE;
                if (!ts_n && a_oe) begin
                    base <= a;
                    write_data <= d;
                    size_q <= size;
                    read_q <= rw_n;
                    second_word <= 0;
                    interrupt_ack <= tt == 2'b11 && tm != 3'd0;
                    // Amiga supplies an autovector. A breakpoint cycle has
                    // no external responder and terminates with a bus error.
                    transfer_error <= tt == 2'b11 && tm == 3'd0;
                    if (tt == 2'b11 || a[31:24] != 0) begin
                        // Kickstart probes above the motherboard's 24-bit bus
                        // for expansion memory. Acknowledge the unmapped cycle
                        // without touching its low-address alias.
                        read_data <= 0;
                        state <= RESPOND;
                    end else state <= REQUEST;
                end
            end
            REQUEST: if (ready) begin
                if (long_access && !wide && !second_word) begin
                    first_word <= data_in;
                    second_word <= 1;
                    state <= WORD_GAP;
                end else begin
                    if (wide) read_data <= {data_in, data_next};
                    else if (second_word) read_data <= {first_word, data_in};
                    else if (base[1]) read_data <= {16'd0, data_in};
                    else read_data <= {data_in, 16'd0};
                    state <= RESPOND;
                end
            end
            WORD_GAP: state <= REQUEST;
            default: state <= IDLE;
        endcase
    end

    ap68040 #(.DC_WRITETHROUGH(1'b1)) cpu (
        .clk(cpu_clk), .bclk_en(cpu_bclk), .rsti_n(reset_n), .rsto_n(cpu_reset_out_n),
        .amiga_fast_top(fast_top),
        .a_o(a), .a_oe(a_oe), .d_o(d), .d_i(read_data), .ts_n(ts_n),
        .rw_n(rw_n), .siz(size), .tt(tt), .tm(tm),
        .ta_n(!(reply && !transfer_error)), .tea_n(!(reply && transfer_error)),
        // Terminate a line as four explicit longword transactions. The
        // memory controller can serve the remaining words from its line fill.
        .tbi_n(1'b0), .tci_n(1'b1), .ipl_n(ipl_n),
        .avec_n(!(reply && interrupt_ack)),
        .bg_n(1'b0), .bb_n_i(1'b1), .cdis_n(1'b1), .mdis_n(1'b1),
        .a_i(32'd0), .ts_n_i(1'b1), .rw_n_i(1'b1),
        .siz_i(2'd0), .tt_i(2'd0), .sc(2'd0)
    );
endmodule

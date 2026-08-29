// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// One-line cartridge cache plus arbitration between the download path and
// live machine accesses. The machine is clock-enable stalled on every miss or
// cartridge-RAM write; the loader has priority while reset is asserted.
module multitini_gameboy_memory (
    input  logic         clk,
    input  logic         reset,
    input  logic         loader_active,

    input  logic [23:0]  loader_addr,
    input  logic         loader_write,
    input  logic [63:0]  loader_wdata,
    input  logic [7:0]   loader_wstrb,
    input  logic         loader_valid,
    output logic         loader_ready,
    output logic [63:0]  loader_rdata,
    output logic         loader_rvalid,

    input  logic         core_read,
    input  logic         core_write,
    input  logic [23:0]  core_addr,
    input  logic [7:0]   core_wdata,
    output logic [7:0]   core_rdata,
    output logic         core_stall,

    output logic [23:0]  memory_addr,
    output logic         memory_write,
    output logic [63:0]  memory_wdata,
    output logic [7:0]   memory_wstrb,
    output logic         memory_valid,
    input  logic         memory_ready,
    input  logic [63:0]  memory_rdata,
    input  logic         memory_rvalid
);
    typedef enum logic [1:0] {
        RUNTIME_IDLE,
        RUNTIME_ISSUE,
        RUNTIME_WAIT
    } runtime_state_t;

    runtime_state_t runtime_state;
    logic [23:0] runtime_addr;
    logic runtime_write;
    logic [63:0] runtime_wdata;
    logic [7:0] runtime_wstrb;
    logic write_served;

    logic cache_valid;
    logic [20:0] cache_tag;
    logic [63:0] cache_data;
    logic cache_hit;

    logic bus_outstanding;
    logic bus_owner_loader;
    logic select_loader;
    logic runtime_accept;

    assign cache_hit = cache_valid && cache_tag == core_addr[23:3];
    assign core_rdata = cache_hit ?
        cache_data[core_addr[2:0] * 8 +: 8] : 8'hff;
    assign core_stall = runtime_state != RUNTIME_IDLE ||
                        (core_read && !cache_hit) ||
                        (core_write && !write_served);

    assign select_loader = loader_valid;
    assign memory_valid = !bus_outstanding &&
                          (select_loader || runtime_state == RUNTIME_ISSUE);
    assign memory_addr = select_loader ? loader_addr : runtime_addr;
    assign memory_write = select_loader ? loader_write : runtime_write;
    assign memory_wdata = select_loader ? loader_wdata : runtime_wdata;
    assign memory_wstrb = select_loader ? loader_wstrb : runtime_wstrb;
    assign loader_ready = !bus_outstanding && select_loader && memory_ready;
    assign runtime_accept = memory_valid && memory_ready && !select_loader;

    always_ff @(posedge clk) begin
        loader_rvalid <= 1'b0;
        if (reset) begin
            runtime_state <= RUNTIME_IDLE;
            runtime_addr <= 24'd0;
            runtime_write <= 1'b0;
            runtime_wdata <= 64'd0;
            runtime_wstrb <= 8'd0;
            write_served <= 1'b0;
            cache_valid <= 1'b0;
            cache_tag <= 21'd0;
            cache_data <= 64'd0;
            bus_outstanding <= 1'b0;
            bus_owner_loader <= 1'b0;
            loader_rdata <= 64'd0;
        end else begin
            if (loader_active)
                cache_valid <= 1'b0;
            if (!core_write)
                write_served <= 1'b0;

            if (memory_valid && memory_ready) begin
                bus_outstanding <= 1'b1;
                bus_owner_loader <= select_loader;
            end
            if (memory_rvalid && bus_outstanding) begin
                bus_outstanding <= 1'b0;
                if (bus_owner_loader) begin
                    loader_rdata <= memory_rdata;
                    loader_rvalid <= 1'b1;
                end else begin
                    cache_valid <= 1'b1;
                    cache_tag <= runtime_addr[23:3];
                    cache_data <= memory_rdata;
                    if (runtime_write)
                        write_served <= 1'b1;
                    runtime_state <= RUNTIME_IDLE;
                end
            end

            unique case (runtime_state)
            RUNTIME_IDLE: begin
                if (!loader_active && core_write && !write_served) begin
                    runtime_addr <= {core_addr[23:3], 3'b000};
                    runtime_write <= 1'b1;
                    runtime_wdata <= {{56{1'b0}}, core_wdata} <<
                                     (core_addr[2:0] * 8);
                    runtime_wstrb <= 8'b00000001 << core_addr[2:0];
                    runtime_state <= RUNTIME_ISSUE;
                end else if (!loader_active && core_read && !cache_hit) begin
                    runtime_addr <= {core_addr[23:3], 3'b000};
                    runtime_write <= 1'b0;
                    runtime_wdata <= 64'd0;
                    runtime_wstrb <= 8'd0;
                    runtime_state <= RUNTIME_ISSUE;
                end
            end
            RUNTIME_ISSUE:
                if (runtime_accept)
                    runtime_state <= RUNTIME_WAIT;
            RUNTIME_WAIT: ;
            default: runtime_state <= RUNTIME_IDLE;
            endcase
        end
    end
endmodule

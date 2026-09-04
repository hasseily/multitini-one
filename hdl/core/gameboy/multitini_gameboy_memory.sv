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

    input  logic         save_read,
    input  logic [23:0]  save_addr,
    input  logic         save_pause,
    output logic         save_ready,
    output logic [63:0]  save_rdata,
    output logic         save_rvalid,

    input  logic         core_read,
    input  logic         core_write,
    input  logic [23:0]  core_addr,
    input  logic [7:0]   core_wdata,
    output logic [7:0]   core_rdata,
    output logic         core_stall,
    output logic         core_write_committed,
    output logic [23:0]  core_write_address,

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
    typedef enum logic [1:0] {
        OWNER_RUNTIME,
        OWNER_LOADER,
        OWNER_SAVE
    } bus_owner_t;
    bus_owner_t bus_owner;
    logic select_loader;
    logic select_save;
    logic runtime_accept;

    assign cache_hit = cache_valid && cache_tag == core_addr[23:3];
    assign core_rdata = cache_hit ?
        cache_data[core_addr[2:0] * 8 +: 8] : 8'hff;
    assign core_stall = save_pause || runtime_state != RUNTIME_IDLE ||
                        (core_read && !cache_hit) ||
                        (core_write && !write_served);

    assign select_loader = loader_valid;
    assign select_save = !select_loader && save_read;
    assign memory_valid = !bus_outstanding &&
                          (select_loader || select_save ||
                           (!save_pause &&
                            runtime_state == RUNTIME_ISSUE));
    assign memory_addr = select_loader ? loader_addr :
                         select_save ? save_addr : runtime_addr;
    assign memory_write = select_loader ? loader_write :
                          select_save ? 1'b0 : runtime_write;
    assign memory_wdata = select_loader ? loader_wdata :
                          select_save ? 64'd0 : runtime_wdata;
    assign memory_wstrb = select_loader ? loader_wstrb :
                          select_save ? 8'd0 : runtime_wstrb;
    assign loader_ready = !bus_outstanding && select_loader && memory_ready;
    assign save_ready = !bus_outstanding && select_save && memory_ready;
    assign runtime_accept = memory_valid && memory_ready &&
                            !select_loader && !select_save;

    always_ff @(posedge clk) begin
        loader_rvalid <= 1'b0;
        save_rvalid <= 1'b0;
        core_write_committed <= 1'b0;
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
            bus_owner <= OWNER_RUNTIME;
            loader_rdata <= 64'd0;
            save_rdata <= 64'd0;
            core_write_address <= 24'd0;
        end else begin
            if (loader_active)
                cache_valid <= 1'b0;
            if (!core_write)
                write_served <= 1'b0;

            if (memory_valid && memory_ready) begin
                bus_outstanding <= 1'b1;
                if (select_loader)
                    bus_owner <= OWNER_LOADER;
                else if (select_save)
                    bus_owner <= OWNER_SAVE;
                else
                    bus_owner <= OWNER_RUNTIME;
            end
            if (memory_rvalid && bus_outstanding) begin
                bus_outstanding <= 1'b0;
                if (bus_owner == OWNER_LOADER) begin
                    loader_rdata <= memory_rdata;
                    loader_rvalid <= 1'b1;
                end else if (bus_owner == OWNER_SAVE) begin
                    save_rdata <= memory_rdata;
                    save_rvalid <= 1'b1;
                end else begin
                    cache_valid <= 1'b1;
                    cache_tag <= runtime_addr[23:3];
                    cache_data <= memory_rdata;
                    if (runtime_write) begin
                        write_served <= 1'b1;
                        core_write_committed <= 1'b1;
                        core_write_address <= runtime_addr;
                    end
                    runtime_state <= RUNTIME_IDLE;
                end
            end

            unique case (runtime_state)
            RUNTIME_IDLE: begin
                if (!loader_active && !save_pause && core_write &&
                    !write_served) begin
                    runtime_addr <= {core_addr[23:3], 3'b000};
                    runtime_write <= 1'b1;
                    runtime_wdata <= {{56{1'b0}}, core_wdata} <<
                                     (core_addr[2:0] * 8);
                    runtime_wstrb <= 8'b00000001 << core_addr[2:0];
                    runtime_state <= RUNTIME_ISSUE;
                end else if (!loader_active && !save_pause && core_read &&
                             !cache_hit) begin
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

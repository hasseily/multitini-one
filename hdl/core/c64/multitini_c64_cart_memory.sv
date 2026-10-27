// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// Arbitrates cartridge loading and live C64 cartridge accesses onto the common
// one-outstanding 64-bit memory service. Reads use a single-line cache.
// Priority is loader, machine, sector erase, then EasyFlash byte traffic, so
// a flash erase or program never holds the running machine off the bus.
module multitini_c64_cart_memory (
    input  logic        clk,
    input  logic        reset,
    input  logic        loader_active,
    input  logic [31:0] erase_request,
    input  logic        erase_allowed,
    output logic [31:0] erase_status,

    input  logic [23:0] loader_addr,
    input  logic        loader_write,
    input  logic [63:0] loader_wdata,
    input  logic [7:0]  loader_wstrb,
    input  logic        loader_valid,
    output logic        loader_ready,

    input  logic        core_request,
    input  logic        core_write,
    input  logic [23:0] core_addr,
    input  logic [7:0]  core_wdata,
    output logic [7:0]  core_rdata,
    output logic        core_stall,
    output logic        busy,
    output logic        loader_busy,
    output logic        fault,
    input  logic        clear_fault,

    // EasyFlash byte reads and writes, one transaction at a time.
    input  logic        flash_valid,
    input  logic        flash_write,
    input  logic [23:0] flash_addr,
    input  logic [7:0]  flash_wdata,
    output logic        flash_accept,
    output logic        flash_done,
    output logic [7:0]  flash_rdata,
    // EasyFlash sector erase on the same engine as the UCI erase command.
    input  logic        flash_erase_valid,
    input  logic [2:0]  flash_erase_group,
    input  logic        flash_erase_high,
    output logic        flash_erase_accept,
    output logic        flash_erase_done,

    output logic [23:0] memory_addr,
    output logic        memory_write,
    output logic [63:0] memory_wdata,
    output logic [7:0]  memory_wstrb,
    output logic        memory_valid,
    input  logic        memory_ready,
    input  logic [63:0] memory_rdata,
    input  logic        memory_rvalid,
    input  logic        memory_fault
);
    typedef enum logic [1:0] {
        RUNTIME_IDLE,
        RUNTIME_ISSUE,
        RUNTIME_WAIT
    } runtime_state_t;

    runtime_state_t runtime_state;
    logic [23:0] runtime_addr;
    logic [2:0] runtime_lane;
    logic runtime_write;
    logic [7:0] runtime_wdata;
    logic core_write_done;
    logic cache_valid;
    logic [20:0] cache_tag;
    logic [63:0] cache_data;
    logic cache_hit;
    logic bus_outstanding;
    logic bus_owner_loader;
    logic select_loader;
    logic runtime_accept;
    logic request_accepted;
    logic response_owner_loader;
    logic bus_owner_erase, response_owner_erase, select_erase;
    logic bus_owner_flash, response_owner_flash, select_flash;
    logic [2:0] flash_lane;
    logic erase_active, erase_wait, erase_error, erase_seen_valid, erase_flash;
    logic [3:0] erase_seen, erase_token, erase_done;
    logic [2:0] erase_group;
    logic erase_high;
    logic [12:0] erase_index;
    wire [23:0] erase_addr = {4'h2, erase_group, erase_index[12:10],
                              erase_high, erase_index[9:0], 3'b000};
    wire runtime_issue = runtime_state == RUNTIME_ISSUE;
    wire uci_erase_new = erase_request[27] &&
        (!erase_seen_valid || erase_request[31:28] != erase_seen);
    always_comb begin
        erase_status = 32'h80000000;
        erase_status[16] = erase_active && !erase_flash;
        erase_status[17] = erase_error;
        erase_status[3:0] = erase_done;
    end

    assign cache_hit = cache_valid && cache_tag == core_addr[23:3];
    assign core_rdata = cache_hit ?
        cache_data[core_addr[2:0] * 8 +: 8] : 8'hff;
    // Once a miss starts, hold the C64 paused even after its original CE pulse
    // ends. Release only when the response has filled the cache (or faults).
    assign core_stall = runtime_state != RUNTIME_IDLE ||
                        (core_request &&
                         (core_write ? !core_write_done : !cache_hit));
    assign busy = bus_outstanding || runtime_state != RUNTIME_IDLE || erase_active;
    assign loader_busy = loader_valid ||
                         (bus_outstanding && bus_owner_loader);

    assign select_loader = loader_valid;
    assign select_erase = erase_active && !erase_wait && !select_loader && !runtime_issue;
    assign select_flash = flash_valid && !select_loader && !runtime_issue && !erase_active;
    assign memory_valid = !bus_outstanding &&
                          (select_loader || runtime_issue || select_erase || select_flash);
    assign memory_addr = select_loader ? loader_addr : runtime_issue ? runtime_addr :
        select_erase ? erase_addr : {flash_addr[23:3], 3'b000};
    assign memory_write = select_loader ? loader_write : runtime_issue ? runtime_write :
        select_erase ? 1'b1 : flash_write;
    assign memory_wdata = select_loader ? loader_wdata :
        runtime_issue ? ({{56{1'b0}}, runtime_wdata} << (runtime_lane * 8)) :
        select_erase ? 64'hffffffffffffffff :
        ({{56{1'b0}}, flash_wdata} << (flash_addr[2:0] * 8));
    assign memory_wstrb = select_loader ? loader_wstrb :
        runtime_issue ? ((runtime_write ? 8'b00000001 : 8'b00000000) << runtime_lane) :
        select_erase ? 8'hff :
        ((flash_write ? 8'b00000001 : 8'b00000000) << flash_addr[2:0]);
    assign loader_ready = !bus_outstanding && select_loader && memory_ready;
    assign request_accepted = memory_valid && memory_ready;
    assign runtime_accept = request_accepted && !select_loader && runtime_issue;
    assign flash_accept = request_accepted && select_flash;
    assign flash_erase_accept = !erase_active && !uci_erase_new && flash_erase_valid &&
                                !loader_active && !loader_valid && !fault;
    // A conforming backend normally responds after acceptance. Selecting the
    // current requester here also handles a zero-latency response without
    // losing write completion or attributing it to the previous owner.
    assign response_owner_loader = bus_outstanding ?
        bus_owner_loader : select_loader;
    assign response_owner_erase = bus_outstanding ? bus_owner_erase : select_erase;
    assign response_owner_flash = bus_outstanding ? bus_owner_flash : select_flash;
    wire [2:0] response_flash_lane = bus_outstanding ? flash_lane : flash_addr[2:0];

    always_ff @(posedge clk) begin
        flash_done <= 1'b0;
        flash_erase_done <= 1'b0;
        if (reset) begin
            runtime_state <= RUNTIME_IDLE;
            runtime_addr <= 24'd0;
            runtime_lane <= 3'd0;
            runtime_write <= 1'b0;
            runtime_wdata <= 8'd0;
            core_write_done <= 1'b0;
            cache_valid <= 1'b0;
            cache_tag <= 21'd0;
            cache_data <= 64'd0;
            bus_outstanding <= 1'b0;
            bus_owner_loader <= 1'b0;
            bus_owner_erase <= 1'b0;
            bus_owner_flash <= 1'b0;
            flash_lane <= 3'd0;
            flash_rdata <= 8'd0;
            erase_active <= 0; erase_wait <= 0; erase_error <= 0; erase_seen_valid <= 0;
            erase_seen <= 0; erase_token <= 0; erase_done <= 0; erase_flash <= 0;
            erase_group <= 0; erase_high <= 0; erase_index <= 0;
            fault <= 1'b0;
        end else begin
            if (clear_fault) fault <= 1'b0;
            if (loader_active) cache_valid <= 1'b0;
            if (!core_request) core_write_done <= 1'b0;

            // The host first requests HOLD and waits for boundary-safe pause.
            // Each 64-bit erase write is separately acknowledged. No erase
            // transaction is attributed to the content loader (or its reset).
            if (!erase_active && uci_erase_new) begin
                erase_seen_valid <= 1;
                erase_seen <= erase_request[31:28];
                erase_token <= erase_request[31:28];
                erase_error <= 0;
                if (!erase_allowed || busy || core_request || loader_active || loader_valid || fault ||
                    (erase_request & 32'h07ffff80) != 0) begin
                    erase_error <= 1;
                    erase_done <= erase_request[31:28];
                end else begin
                    erase_active <= 1;
                    erase_flash <= 0;
                    erase_wait <= 0;
                    erase_group <= erase_request[5:3];
                    erase_high <= erase_request[6];
                    erase_index <= 0;
                    cache_valid <= 0;
                end
            end else if (flash_erase_accept) begin
                // The machine keeps running; only erased lines leave the cache.
                erase_active <= 1;
                erase_flash <= 1;
                erase_wait <= 0;
                erase_group <= flash_erase_group;
                erase_high <= flash_erase_high;
                erase_index <= 0;
            end
            // New content replaces the whole cartridge image.
            if (loader_active && erase_active && erase_flash) erase_active <= 0;

            if (request_accepted) begin
                bus_outstanding <= 1'b1;
                bus_owner_loader <= select_loader;
                bus_owner_erase <= select_erase;
                bus_owner_flash <= select_flash;
                flash_lane <= flash_addr[2:0];
                if (select_erase) begin
                    erase_wait <= 1;
                    if (cache_tag == erase_addr[23:3]) cache_valid <= 1'b0;
                end
                if (select_flash && flash_write && cache_tag == flash_addr[23:3])
                    cache_valid <= 1'b0;
            end
            if (memory_rvalid && (bus_outstanding || request_accepted)) begin
                bus_outstanding <= 1'b0;
                if (response_owner_erase) begin
                    erase_wait <= 0;
                    if (erase_index == 13'd8191) begin
                        erase_active <= 0;
                        if (erase_flash) flash_erase_done <= 1;
                        else erase_done <= erase_token;
                    end else erase_index <= erase_index + 1'b1;
                end else if (response_owner_flash) begin
                    flash_done <= 1'b1;
                    flash_rdata <= memory_rdata[response_flash_lane * 8 +: 8];
                end else if (!response_owner_loader) begin
                    if (runtime_write) begin
                        if (cache_valid &&
                            cache_tag == runtime_addr[23:3])
                            cache_valid <= 1'b0;
                        core_write_done <= 1'b1;
                    end else begin
                        cache_valid <= 1'b1;
                        cache_tag <= runtime_addr[23:3];
                        cache_data <= memory_rdata;
                    end
                    runtime_state <= RUNTIME_IDLE;
                end
            end
            if (memory_fault) begin
                bus_outstanding <= 1'b0;
                runtime_state <= RUNTIME_IDLE;
                cache_valid <= 1'b0;
                fault <= 1'b1;
                if (erase_active) begin
                    erase_active <= 0;
                    erase_wait <= 0;
                    if (erase_flash) flash_erase_done <= 1;
                    else begin
                        erase_error <= 1;
                        erase_done <= erase_token;
                    end
                end
                if (bus_owner_flash || select_flash) flash_done <= 1'b1;
            end

            unique case (runtime_state)
                RUNTIME_IDLE:
                    if (!loader_active && core_request &&
                        (core_write ? !core_write_done : !cache_hit)) begin
                        runtime_addr <= {core_addr[23:3], 3'b000};
                        runtime_lane <= core_addr[2:0];
                        runtime_write <= core_write;
                        runtime_wdata <= core_wdata;
                        runtime_state <= RUNTIME_ISSUE;
                    end
                RUNTIME_ISSUE:
                    if (runtime_accept) begin
                        if (memory_rvalid)
                            runtime_state <= RUNTIME_IDLE;
                        else
                            runtime_state <= RUNTIME_WAIT;
                    end
                RUNTIME_WAIT: ;
                default: runtime_state <= RUNTIME_IDLE;
            endcase
        end
    end
endmodule

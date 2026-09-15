// SPDX-License-Identifier: GPL-3.0-only
// Fixed-rate, single-clock wrapper for the pinned mc6809i CPU.
module multitini_vectrex_cpu #(
    parameter integer CLOCKS_PER_CPU = 16
) (
    input  wire         clk,
    input  wire         reset,
    input  wire         pause_request,
    output wire         paused,
    input  wire         irq,
    input  wire         firq,
    input  wire         nmi,
    input  wire [7:0]   data_in,
    output wire [15:0]  address,
    output wire [7:0]   data_out,
    output wire         bus_read,
    output wire         bus_write,
    output wire         cpu_tick,
    output wire         e_tick,
    output wire         bus_valid,
    output wire         instruction_fetch,
    output wire [111:0] debug_registers
);
    localparam integer PHASE_WIDTH = $clog2(CLOCKS_PER_CPU);
    localparam integer Q_PHASE = (CLOCKS_PER_CPU * 3) / 4;
    reg [PHASE_WIDTH-1:0] phase;
    reg valid;
    reg [7:0] read_latch;
    wire read_not_write;
    wire advance_valid;

    // Stop only at the next E falling edge, after the previous bus transfer.
    // Keep all state and the captured read value intact while stopped.
    assign paused = !reset && phase == 0 && pause_request;
    assign e_tick = !reset && !paused && phase == 0;
    assign cpu_tick = !reset && phase == Q_PHASE;
    assign bus_valid = !reset && valid;
    assign bus_read = cpu_tick && valid && read_not_write;
    assign bus_write = cpu_tick && valid && !read_not_write;

    always @(posedge clk) begin
        if (reset) begin
            phase <= 0;
            valid <= 1'b0;
            read_latch <= 8'hff;
        end else begin
            if (!paused) begin
                if (phase == CLOCKS_PER_CPU - 1)
                    phase <= 0;
                else
                    phase <= phase + 1'b1;
            end
            // AVMA describes the following cycle, as in the upstream wrapper.
            if (e_tick)
                valid <= advance_valid;
            // Sample before a peripheral clears read-sensitive flags. The CPU
            // consumes this stable value on the following E falling edge.
            if (cpu_tick)
                read_latch <= data_in;
        end
    end

    mc6809i cpu (
        .clk(clk), .cen_E(e_tick), .cen_Q(cpu_tick),
        .nRESET(!reset), .nIRQ(!irq), .nFIRQ(!firq), .nNMI(!nmi),
        .nHALT(1'b1), .nDMABREQ(1'b1),
        .D(read_latch), .DOut(data_out), .ADDR(address),
        .RnW(read_not_write), .AVMA(advance_valid),
        .BA(), .BS(), .BUSY(), .LIC(),
        .OP(instruction_fetch), .RegData(debug_registers)
    );

`ifndef SYNTHESIS
    initial begin
        if (CLOCKS_PER_CPU < 4 || CLOCKS_PER_CPU % 4 != 0)
            $fatal(1, "CLOCKS_PER_CPU must be at least four and divisible by four");
    end
`endif
endmodule

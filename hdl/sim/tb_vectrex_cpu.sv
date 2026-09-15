// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps

module vectrex_cpu_case #(parameter integer DIVIDER = 16)(
    input wire clk,
    output reg done = 0
);
    reg reset = 1;
    reg pause_request = 0;
    reg irq = 0, firq = 0, nmi = 0;
    wire paused, bus_read, bus_write, cpu_tick, e_tick, bus_valid, fetch;
    wire [15:0] address;
    wire [7:0] data_out;
    wire [111:0] registers;
    reg [7:0] memory [0:65535];
    reg [7:0] memory_q;
    integer writes [0:65535];
    integer p, i, cycles = 0, last_fetch_cycle = 0;
    integer lda_pc, adda_pc, sta_pc, after_sta_pc;
    integer checked_cycles = 0;
    reg previous_strobe = 0;
    reg [7:0] status = 8'h80;
    wire [7:0] data_in = address == 16'hc900 ? status : memory_q;

    multitini_vectrex_cpu #(.CLOCKS_PER_CPU(DIVIDER)) dut (
        .clk(clk), .reset(reset), .pause_request(pause_request), .paused(paused),
        .irq(irq), .firq(firq), .nmi(nmi), .data_in(data_in),
        .address(address), .data_out(data_out), .bus_read(bus_read),
        .bus_write(bus_write), .cpu_tick(cpu_tick), .e_tick(e_tick),
        .bus_valid(bus_valid), .instruction_fetch(fetch),
        .debug_registers(registers)
    );

    // A one-fabric-cycle synchronous RAM read checks the wrapper's real
    // integration contract. The read-sensitive status register is separate.
    always @(posedge clk) begin
        memory_q <= memory[address];
        if (bus_write) begin
            memory[address] <= data_out;
            writes[address] <= writes[address] + 1;
        end
        if (bus_read && address == 16'hc900)
            status <= 0;
        if (reset && (bus_read || bus_write || cpu_tick))
            $fatal(1, "DIV=%0d: bus activity during reset", DIVIDER);
        if (bus_read && bus_write)
            $fatal(1, "DIV=%0d: simultaneous read/write", DIVIDER);
        if ((bus_read || bus_write) && (!cpu_tick || previous_strobe))
            $fatal(1, "DIV=%0d: repeated or misplaced strobe", DIVIDER);
        if (paused && (bus_read || bus_write || cpu_tick || e_tick))
            $fatal(1, "DIV=%0d: activity during pause", DIVIDER);
        previous_strobe <= bus_read || bus_write;
        if (e_tick)
            cycles <= cycles + 1;
        if (bus_read && fetch) begin
            if (address == adda_pc) begin
                if (cycles - last_fetch_cycle != 2)
                    $fatal(1, "LDA immediate cycle count");
                checked_cycles <= checked_cycles + 1;
            end
            if (address == sta_pc) begin
                if (cycles - last_fetch_cycle != 2)
                    $fatal(1, "ADDA immediate cycle count");
                checked_cycles <= checked_cycles + 1;
            end
            if (address == after_sta_pc) begin
                if (cycles - last_fetch_cycle != 5)
                    $fatal(1, "STA extended cycle count");
                checked_cycles <= checked_cycles + 1;
            end
            last_fetch_cycle <= cycles;
        end
    end

    // All instruction bytes below are an original, deliberately tiny test.
    task emit(input [7:0] value);
        begin memory[p] = value; p = p + 1; end
    endtask
    task expect_byte(input [15:0] location, input [7:0] value);
        begin
            if (memory[location] !== value || writes[location] != 1)
                $fatal(1, "DIV=%0d: [%04x]=%02x expected %02x, writes=%0d",
                    DIVIDER, location, memory[location], value, writes[location]);
        end
    endtask
    task wait_write(input [15:0] location);
        integer timeout;
        begin
            timeout = 0;
            while (writes[location] == 0 && timeout < DIVIDER * 1000) begin
                @(negedge clk);
                timeout = timeout + 1;
            end
            if (writes[location] == 0)
                $fatal(1, "DIV=%0d: timed out at %04x, CPU PC=%04x address=%04x",
                    DIVIDER, location, registers[111:96], address);
        end
    endtask

    initial begin
        for (i = 0; i < 65536; i = i + 1) begin
            memory[i] = 8'h12;
            writes[i] = 0;
        end
        memory[16'hfffe] = 8'hf0; memory[16'hffff] = 0;
        memory[16'hfff8] = 8'hf1; memory[16'hfff9] = 0;
        memory[16'hfff6] = 8'hf1; memory[16'hfff7] = 8'h10;
        memory[16'hfffc] = 8'hf1; memory[16'hfffd] = 8'h20;
        p = 16'hf000;
        emit(8'h10); emit(8'hce); emit(8'hcf); emit(8'hff); // LDS #$cfff
        emit(8'h1c); emit(8'haf);                         // ANDCC #$af
        lda_pc = p; emit(8'h86); emit(8'h12);             // LDA #$12
        adda_pc = p; emit(8'h8b); emit(8'h34);            // ADDA #$34
        sta_pc = p; emit(8'hb7); emit(8'hc8); emit(0);     // STA $c800
        after_sta_pc = p;
        emit(8'hc6); emit(3);                            // LDB #3
        emit(8'h8e); emit(8'hc8); emit(8'h20);            // LDX #$c820
        emit(8'ha7); emit(8'h80);                        // STA ,X+
        emit(8'h5a); emit(8'h26); emit(8'hfb);            // DECB / BNE loop
        emit(8'hbd); emit(8'hf2); emit(0);                // JSR $f200
        emit(8'hb6); emit(8'hc9); emit(0);                // LDA $c900 (clear on read)
        emit(8'hb7); emit(8'hc8); emit(4);                // STA $c804
        emit(8'h86); emit(8'ha5);
        emit(8'hb7); emit(8'hc8); emit(3);                // finished marker
        emit(8'h20); emit(8'hfe);                        // BRA forever

        p = 16'hf200;
        emit(8'h86); emit(8'h55);
        emit(8'hb7); emit(8'hc8); emit(2); emit(8'h39);    // STA $c802 / RTS
        p = 16'hf100;
        emit(8'h86); emit(8'h11);
        emit(8'hb7); emit(8'hc8); emit(8'h10); emit(8'h3b);// IRQ / RTI
        p = 16'hf110;
        emit(8'h86); emit(8'h22);
        emit(8'hb7); emit(8'hc8); emit(8'h11); emit(8'h3b);// FIRQ / RTI
        p = 16'hf120;
        emit(8'h86); emit(8'h33);
        emit(8'hb7); emit(8'hc8); emit(8'h12); emit(8'h3b);// NMI / RTI

        repeat (5) @(negedge clk);
        reset = 0;
        wait_write(16'hc800);
        // Assert pause within a cycle, then require a clean boundary stop.
        repeat (3) @(negedge clk);
        pause_request = 1;
        wait(paused);
        repeat (DIVIDER * 3) @(negedge clk);
        pause_request = 0;
        wait_write(16'hc803);
        expect_byte(16'hc800, 8'h46);
        expect_byte(16'hc802, 8'h55);
        expect_byte(16'hc804, 8'h80);
        expect_byte(16'hc820, 8'h46);
        expect_byte(16'hc821, 8'h46);
        expect_byte(16'hc822, 8'h46);
        if (checked_cycles != 3 || status != 0)
            $fatal(1, "DIV=%0d: missing cycle/read-side-effect checks", DIVIDER);

        irq = 1;
        wait_write(16'hc810);
        irq = 0;
        repeat (DIVIDER * 40) @(negedge clk);
        expect_byte(16'hc810, 8'h11);
        if (registers[63:48] != 16'hcfff)
            $fatal(1, "IRQ RTI did not restore stack");
        firq = 1;
        wait_write(16'hc811);
        firq = 0;
        repeat (DIVIDER * 40) @(negedge clk);
        expect_byte(16'hc811, 8'h22);
        if (registers[63:48] != 16'hcfff)
            $fatal(1, "FIRQ RTI did not restore stack");
        nmi = 1;
        wait_write(16'hc812);
        nmi = 0;
        repeat (DIVIDER * 40) @(negedge clk);
        expect_byte(16'hc812, 8'h33);
        if (registers[63:48] != 16'hcfff)
            $fatal(1, "NMI RTI did not restore stack");
        $display("PASS: Vectrex CPU divider=%0d instructions, bus timing, IRQ/FIRQ/NMI, pause", DIVIDER);
        done = 1;
    end
endmodule

module tb_vectrex_cpu;
    reg clk = 0;
    wire done16, done64;
    always #5 clk = !clk;
    vectrex_cpu_case #(.DIVIDER(16)) case16(clk, done16);
    vectrex_cpu_case #(.DIVIDER(64)) case64(clk, done64);
    initial begin
        wait(done16 && done64);
        $display("PASS: tb_vectrex_cpu");
        $finish;
    end
    initial begin
        #10000000;
        $fatal(1, "Vectrex CPU global watchdog");
    end
endmodule

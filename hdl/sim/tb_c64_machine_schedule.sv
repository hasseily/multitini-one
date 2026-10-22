// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// The runner adds observation-only ports to an exact copy of the production
// FPGA64 entity. CPU, PLA/ROM, VIC, CIA, SID and local RAM are their real RTL.
// Synthetic ROMs/program are original test data and need no Commodore assets.
module tb_c64_machine_schedule;
    logic clk = 0, reset_n = 0, pause = 1, memory_wait = 0;
    logic ntsc = 0, local_turbo_allowed = 1;
    logic [1:0] turbo_mode = 0, turbo_speed = 0, bios = 0;
    logic rom_we = 0, loader_we = 0;
    logic [13:0] rom_addr = 0;
    logic [7:0] rom_data = 0, loader_data = 0;
    logic [15:0] loader_addr = 0;
    logic dma_req = 0, dma_we = 0;
    logic [15:0] dma_addr = 16'h4000;
    logic [7:0] dma_data = 8'hA7;
    wire [7:0] dma_result;
    wire dma_cycle;
    wire [15:0] ram_addr;
    wire [7:0] ram_data, ram_q;
    wire ram_ce, ram_we, paused;
    wire [4:0] dbg_phase;
    wire [15:0] dbg_cpu_addr;
    wire [7:0] dbg_cpu_di, dbg_cpu_do, dbg_cpu_port, dbg_vic_bus;
    wire dbg_cpu_we, dbg_request, dbg_retire, dbg_reset, dbg_hold;
    wire dbg_rdy, dbg_cpu_owned, dbg_dma;
    wire [5:0] dbg_native_ce;
    wire [4:0] dbg_io_cs;
    wire dbg_io_write;
    logic [7:0] initial_ram [0:65535];
    logic [24:0] baseline [0:2047];
    integer baseline_count [0:1];
    integer variant = 0, trace_count = 0, run_clocks = 0;
    integer native_ticks = 0, pixel_ticks = 0, vic_ticks = 0;
    integer cia_p_ticks = 0, cia_n_ticks = 0;
    integer io_writes [0:4];
    integer sid_write_pulses = 0, color_write_pulses = 0;
    integer extra_retires = 0, wait_checks = 0, pause_checks = 0, dma_checks = 0;
    integer held_retire_checks = 0, held_pixel_checks = 0, dma_transfers = 0;
    integer last_sid_tick = -1, active_ticks = 0;
    logic running = 0, tracing = 0, finished = 0, count_native = 0;
    logic [7:0] bus_value;
    logic [24:0] bus_record;
    logic [7:0] port_ddr = 0, ram_port_expected [0:1];
    integer port_backing_writes = 0;

    always #5 clk = !clk;
    multitini_c64_local_ram memory_i (
        .clk(clk), .loader_we(loader_we), .loader_addr(loader_addr),
        .loader_data(loader_data), .machine_ce(ram_ce), .machine_we(ram_we),
        .machine_store(1'b1), .machine_addr(ram_addr), .machine_data(ram_data),
        .machine_q(ram_q)
    );

    fpga64_sid_iec dut (
        .clk32(clk), .reset_n(reset_n), .bios(bios), .pause(pause),
        .memory_wait(memory_wait), .pause_out(paused),
        .ps2_key(11'd0), .kbd_reset(!reset_n), .shift_mod(2'b11),
        .ramAddr(ram_addr), .ramDin(ram_q), .ramDout(ram_data),
        .ramCE(ram_ce), .ramWE(ram_we), .io_cycle(), .ext_cycle(), .refresh(),
        .cia_mode(1'b0), .turbo_mode(turbo_mode), .turbo_speed(turbo_speed),
        .local_turbo_allowed(local_turbo_allowed), .vic_variant(2'b00),
        .ntscMode(ntsc), .hsync(), .vsync(), .palette(3'b000), .r(), .g(), .b(),
        .game(1'b1), .exrom(1'b1), .io_rom(1'b0), .io_ext(1'b0), .io_data(8'hFF),
        .irq_n(1'b1), .nmi_n(1'b1), .nmi_ack(), .romL(), .romH(), .UMAXromH(),
        .IOE(), .IOF(), .freeze_key(), .mod_key(), .tape_play(), .tape_rew(),
        .tape_stop(), .tape_ff(), .tape_reset_counter(),
        .dma_req(dma_req), .dma_cycle(dma_cycle), .dma_addr(dma_addr),
        .dma_dout(dma_data), .dma_din(dma_result), .dma_we(dma_we), .irq_ext_n(1'b1),
        .joyA(7'h7F), .joyB(7'h7F), .pot1(8'hFF), .pot2(8'hFF),
        .pot3(8'hFF), .pot4(8'hFF), .audio_l(), .audio_r(),
        .sid_filter(2'b11), .sid_ver(2'b00), .sid_mode(3'b000), .sid_cfg(4'b0000),
        .sid_fc_off_l(13'd0), .sid_fc_off_r(13'd0), .sid_ld_clk(clk),
        .sid_ld_addr(12'd0), .sid_ld_data(16'd0), .sid_ld_wr(1'b0), .sid_digifix(1'b0),
        .pb_i(8'hFF), .pb_o(), .pb_oe_o(), .pa2_i(1'b1), .pa2_o(), .pc2_n_o(),
        .flag2_n_i(1'b1), .sp2_i(1'b1), .sp2_o(), .sp1_i(1'b1), .sp1_o(),
        .cnt2_i(1'b1), .cnt2_o(), .cnt1_i(1'b1), .cnt1_o(),
        .iec_data_o(), .iec_data_i(1'b1), .iec_clk_o(), .iec_clk_i(1'b1), .iec_atn_o(),
        .c64rom_addr(rom_addr), .c64rom_data(rom_data), .c64rom_wr(rom_we),
        .cass_motor(), .cass_write(), .cass_sense(1'b1), .cass_read(1'b1),
        .dbg_phase(dbg_phase), .dbg_cpu_addr(dbg_cpu_addr), .dbg_cpu_di(dbg_cpu_di),
        .dbg_cpu_do(dbg_cpu_do), .dbg_cpu_we(dbg_cpu_we), .dbg_request(dbg_request),
        .dbg_cpu_port(dbg_cpu_port), .dbg_vic_bus(dbg_vic_bus),
        .dbg_retire(dbg_retire), .dbg_reset(dbg_reset), .dbg_hold(dbg_hold),
        .dbg_rdy(dbg_rdy), .dbg_cpu_owned(dbg_cpu_owned), .dbg_dma(dbg_dma),
        .dbg_native_ce(dbg_native_ce), .dbg_io_cs(dbg_io_cs), .dbg_io_write(dbg_io_write)
    );

    task automatic check(input logic okay, input string message);
        if (!okay) $fatal(1, "FAIL: machine ntsc=%0d variant=%0d %s", ntsc, variant, message);
    endtask

    always @(posedge clk) begin
        if (running && !dbg_reset) begin
            if (ram_ce && ram_we && ram_addr < 2) begin
                check(ram_data === dbg_vic_bus, "RAM beneath 6510 port receives VIC bus byte");
                ram_port_expected[ram_addr] = dbg_vic_bus;
                port_backing_writes++;
            end
            if (dbg_hold) begin
                check(!dbg_request && !dbg_retire && !ram_ce && dbg_native_ce == 0,
                      "memory wait suppresses transaction and native enables");
                wait_checks++;
                if (dbg_phase == 31) begin
                    check(dbg_cpu_owned, "CPU ownership survives a held CPUF retirement");
                    held_retire_checks++;
                end
                if (dbg_phase == 15) held_pixel_checks++;
            end
            if (dbg_dma) begin
                check(!dbg_retire, "DMA ownership stalls CPU retirement");
                dma_checks++;
            end
            if (count_native && !paused && !dbg_hold) begin
                active_ticks++;
                if (dbg_native_ce[0]) begin
                    if (last_sid_tick >= 0)
                        check(active_ticks - last_sid_tick == 32, "SID keeps native 32-clock period");
                    last_sid_tick = active_ticks;
                    native_ticks++;
                end
                if (dbg_native_ce[1]) cia_p_ticks++;
                if (dbg_native_ce[2]) cia_n_ticks++;
                if (dbg_native_ce[3]) vic_ticks++;
                if (dbg_native_ce[4]) pixel_ticks++;
            end
            if (dbg_request && dbg_cpu_owned && !dbg_dma && dbg_cpu_we)
                for (integer j = 0; j < 5; j++)
                    if (dbg_io_cs[j]) io_writes[j]++;
            if (dbg_io_write) begin
                if (dbg_io_cs[1]) sid_write_pulses++;
                if (dbg_io_cs[2]) color_write_pulses++;
            end
            if (dbg_retire && (dbg_rdy || dbg_cpu_we)) begin
                if (dbg_phase != 31) extra_retires++;
                if (!dbg_cpu_we && dbg_cpu_addr == 16'h0800) tracing = 1;
                if (tracing && !finished) begin
                    bus_value = dbg_cpu_we ? dbg_cpu_do : dbg_cpu_di;
                    if (dbg_cpu_we && dbg_cpu_addr == 0) port_ddr = dbg_cpu_do;
                    // Color RAM's upper nibble is the VIC floating bus. It can
                    // differ when CPU speed changes; the test masks it in software.
                    if (!dbg_cpu_we && dbg_cpu_addr == 16'hD800) bus_value &= 8'h0F;
                    bus_record = {dbg_cpu_addr, dbg_cpu_we, bus_value};
                    check(trace_count < 2048, "bounded CPU trace");
                    if (variant == 0) baseline[trace_count] = bus_record;
                    else check(baseline[trace_count] === bus_record,
                        $sformatf("accepted NMOS bus trace at %0d expected=%07h got=%07h",
                                  trace_count, baseline[trace_count], bus_record));
                    trace_count++;
                    if (dbg_cpu_we && dbg_cpu_addr == 16'h2105 && dbg_cpu_do == 8'hA5)
                        finished = 1;
                end
            end
        end
    end

    task automatic load_roms;
        @(negedge clk); bios = 0; rom_we = 1;
        for (integer address = 0; address < 16384; address++) begin
            rom_addr = address;
            rom_data = address == 0 ? 8'h42 : address == 14'h3FFC ? 8'h00 :
                       address == 14'h3FFD ? 8'h08 : 8'hEA;
            @(negedge clk);
        end
        bios = 1;
        for (integer address = 0; address < 4096; address++) begin
            rom_addr = address; rom_data = 8'hC7;
            @(negedge clk);
        end
        rom_we = 0; bios = 0;
    endtask

    task automatic dma_transfer(input logic write_cycle, input logic [15:0] address,
                                input logic [7:0] value, input logic [7:0] expected);
        integer timeout_clocks;
        logic [7:0] pins_before;
        @(negedge clk);
        pins_before = dbg_cpu_port;
        dma_req = 1; dma_we = write_cycle; dma_addr = address; dma_data = value;
        timeout_clocks = 0;
        while (!(dma_cycle && dbg_phase == 28) && timeout_clocks < 256) begin
            @(negedge clk); timeout_clocks++;
        end
        check(timeout_clocks < 256 && dbg_request && ram_ce,
              "DMA gains ownership and issues one native memory transaction");
        @(negedge clk);
        while (dbg_phase != 31) @(negedge clk);
        if (!write_cycle)
            check(dma_result === expected, $sformatf("DMA read %04h expected %02h got %02h",
                                                   address, expected, dma_result));
        else if (address >= 2)
            check(memory_i.ram[address] === value, "DMA write reaches ordinary RAM");
        else
            check(memory_i.ram[address] === ram_port_expected[address],
                  "DMA write beneath 6510 port stores VIC bus byte");
        check(dbg_cpu_port === pins_before, "DMA leaves internal 6510 port unchanged");
        dma_transfers++;
        dma_req = 0; dma_we = 0;
        while (dbg_dma) @(negedge clk);
    endtask

    task automatic run_case(input integer selected);
        integer hold_remaining, dma_remaining, pause_remaining, paused_trace;
        logic injected_wait, injected_pause, injected_dma, injected_retire_wait, injected_pixel_wait;
        variant = selected;
        @(negedge clk); running = 0; reset_n = 0; pause = 1;
        memory_wait = 0; dma_req = 0; dma_we = 0;
        repeat (160) @(negedge clk);
        check(dbg_reset && paused, "reset and pause acknowledged before loading");
        loader_we = 1;
        for (integer address = 0; address < 65536; address++) begin
            loader_addr = address; loader_data = initial_ram[address];
            @(negedge clk);
        end
        loader_we = 0;
        turbo_mode = selected == 0 ? 2'b00 : 2'b10;
        turbo_speed = selected == 0 ? 0 : selected > 3 ? 3 : selected - 1;
        local_turbo_allowed = selected != 5;
        trace_count = 0; run_clocks = 0; extra_retires = 0;
        port_ddr = 0; port_backing_writes = 0;
        tracing = 0; finished = 0;
        for (integer j = 0; j < 5; j++) io_writes[j] = 0;
        sid_write_pulses = 0; color_write_pulses = 0;
        hold_remaining = 0; dma_remaining = 0; pause_remaining = 0;
        injected_wait = 0; injected_pause = 0; injected_dma = 0;
        injected_retire_wait = 0; injected_pixel_wait = 0;
        native_ticks = 0; pixel_ticks = 0; vic_ticks = 0; cia_p_ticks = 0; cia_n_ticks = 0;
        active_ticks = 0; last_sid_tick = -1; count_native = 0;
        reset_n = 1; pause = 0; running = 1;
        while (!finished && run_clocks < 30000) begin
            @(negedge clk);
            // Start the peripheral cadence sample at a full frame boundary.
            if (!dbg_reset && dbg_phase == 0 && !count_native) count_native = 1;
            if (selected == 4 && tracing) begin
                if (!injected_wait && dbg_request && dbg_cpu_addr == 16'h2000) begin
                    memory_wait = 1; hold_remaining = 7; injected_wait = 1;
                end else if (hold_remaining > 0) begin
                    hold_remaining--;
                    if (hold_remaining == 0) memory_wait = 0;
                end else if (injected_wait && !injected_retire_wait && dbg_phase == 30 && dbg_cpu_owned) begin
                    // The wait input is registered, so phase30 freezes phase31
                    // with the native CPU request still pending retirement.
                    memory_wait = 1; hold_remaining = 7; injected_retire_wait = 1;
                end else if (injected_retire_wait && !injected_pixel_wait && dbg_phase == 14) begin
                    memory_wait = 1; hold_remaining = 7; injected_pixel_wait = 1;
                end
                if (!injected_pause && trace_count > 55 && hold_remaining == 0) begin
                    pause = 1; injected_pause = 1;
                end
                if (pause && paused) begin
                    if (pause_remaining == 0) paused_trace = trace_count;
                    check(trace_count == paused_trace, "acknowledged pause retains CPU state");
                    pause_checks++; pause_remaining++;
                    if (pause_remaining == 40) pause = 0;
                end
                if (!injected_dma && trace_count > 80 && !pause && dbg_phase == 29) begin
                    dma_req = 1; dma_remaining = 100; injected_dma = 1;
                end else if (dma_remaining > 0) begin
                    dma_remaining--;
                    if (dma_remaining == 0) dma_req = 0;
                end
            end
            run_clocks++;
        end
        check(finished, "synthetic program completed");
        if (selected == 0) baseline_count[ntsc] = trace_count;
        else check(trace_count == baseline_count[ntsc], "all accepted CPU cycles match stock");
        check(memory_i.ram[16'h2000] === 8'h24, "NMOS INC/SLO old/new stores");
        check(memory_i.ram[16'h2100] === 8'h35, "undocumented SLO result");
        check(memory_i.ram[16'h2101] === 8'h42, "BASIC ROM through PLA");
        check(memory_i.ram[16'h2102] === 8'hA6, "RAM beneath BASIC after port change");
        check(memory_i.ram[16'h2103] === 8'hC7, "character ROM after port change");
        check(memory_i.ram[16'h2104] === 8'h07, "color RAM after restoring I/O");
        // MAME's retained floating bits 6/7 remain high until their native
        // 350000-cycle falloff, so reading latch $37 with DDR $2F gives $F7.
        check(memory_i.ram[16'h2106] === 8'h2F && memory_i.ram[16'h2107] === 8'hF7,
              $sformatf("internal 6510 DDR/port readback expected 2F/F7, got %02h/%02h",
                        memory_i.ram[16'h2106], memory_i.ram[16'h2107]));
        check(port_backing_writes == 5 && memory_i.ram[0] === ram_port_expected[0] &&
              memory_i.ram[1] === ram_port_expected[1], "MAME external RAM0/1 writes observed");
        check(io_writes[0] == 2 && io_writes[1] == 1 && io_writes[2] == 1 &&
              io_writes[3] == 1 && io_writes[4] == 0, "one accepted transaction per programmed I/O write");
        check(sid_write_pulses == 1 && color_write_pulses == 1,
              "SID/color write strobes execute exactly once");
        check(native_ticks > 10 && cia_p_ticks >= native_ticks - 1 &&
              cia_p_ticks <= native_ticks + 1 && cia_n_ticks >= native_ticks - 1 &&
              cia_n_ticks <= native_ticks + 1, "CIA and SID keep independent native cadence");
        check(vic_ticks >= native_ticks * 2 - 2 && vic_ticks <= native_ticks * 2 + 2 &&
              pixel_ticks >= native_ticks * 8 - 8 && pixel_ticks <= native_ticks * 8 + 8,
              "VIC data and pixel cadence stay native");
        if (selected == 0 || selected == 5) check(extra_retires == 0, "stock/external-memory guard forbids extra CPU retirements");
        else check(extra_retires > 0, "accelerated mode actually executes extra CPU transactions");
        if (selected == 4) check(injected_wait && injected_retire_wait && injected_pixel_wait && injected_pause && injected_dma,
                                "wait, pause and DMA perturbations exercised");
        if (selected == 4) begin
            dma_transfer(1, 16'h4000, 8'hA7, 0);
            dma_transfer(0, 16'h4000, 0, 8'hA7);
            dma_transfer(1, 16'h0000, 8'h55, 0);
            dma_transfer(0, 16'h0000, 0, ram_port_expected[0]);
            dma_transfer(1, 16'h0001, 8'hAA, 0);
            dma_transfer(0, 16'h0001, 0, ram_port_expected[1]);
            check(port_backing_writes == 7, "DMA port writes each execute exactly once");
        end
        $display("MACHINE_SUMMARY ntsc=%0d mode=%0d bus_cycles=%0d clocks=%0d extras=%0d native=%0d",
                 ntsc, selected, trace_count, run_clocks, extra_retires, native_ticks);
        running = 0; count_native = 0;
    endtask

    initial begin
        $readmemh("machine_memory.hex", initial_ram);
        load_roms();
        for (integer standard = 0; standard < 2; standard++) begin
            ntsc = standard;
            for (integer mode = 0; mode < 6; mode++) run_case(mode);
        end
        check(wait_checks > 0 && pause_checks >= 80 && dma_checks > 0, "interruption coverage");
        check(held_retire_checks > 0 && held_pixel_checks > 0 && dma_transfers == 12,
              "pending retire/pixel holds and directed DMA transfer coverage");
        $display("PASS: C64 production machine PAL/NTSC stock,2x,3x,4x,dense,guarded memory; native VIC/CIA/SID, ROM banking, NMOS bus trace, wait/pause/DMA");
        $finish;
    end
    initial begin #20000000; $fatal(1, "FAIL: production C64 schedule timeout"); end
endmodule

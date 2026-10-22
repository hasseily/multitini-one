// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// The runner adds observation-only ports to an exact copy of the production
// FPGA64 entity. CPU, PLA/ROM, VIC, CIA, SID and local RAM are their real RTL.
// Synthetic ROMs/program are original test data and need no Commodore assets.
module tb_c64_reu_machine;
    logic clk = 0, reset_n = 0, pause = 1, memory_wait = 0;
    logic ntsc = 0, local_turbo_allowed = 1;
    logic [1:0] turbo_mode = 0, turbo_speed = 0, bios = 0;
    logic rom_we = 0, loader_we = 0;
    logic [13:0] rom_addr = 0;
    logic [7:0] rom_data = 0, loader_data = 0;
    logic [15:0] loader_addr = 0;
    wire dma_req, dma_we, dma_valid, dma_ack, cpu_bus_valid, cpu_bus_write, iof, reu_irq, reu_fault;
    wire [15:0] cpu_bus_addr;
    wire [7:0] cpu_bus_wdata, reu_io_data;
    wire [15:0] dma_addr;
    wire [7:0] dma_data;
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
        .game(1'b1), .exrom(1'b1), .io_rom(1'b0), .io_ext(iof && !dma_req), .io_data(reu_io_data),
        .irq_n(1'b1), .nmi_n(1'b1), .nmi_ack(), .romL(), .romH(), .UMAXromH(),
        .IOE(), .IOF(iof), .freeze_key(), .mod_key(), .tape_play(), .tape_rew(),
        .tape_stop(), .tape_ff(), .tape_reset_counter(),
        .dma_req(dma_req), .dma_valid(dma_valid), .dma_ack(dma_ack),
        .cpu_bus_valid(cpu_bus_valid), .cpu_bus_addr(cpu_bus_addr),
        .cpu_bus_wdata(cpu_bus_wdata), .cpu_bus_write(cpu_bus_write), .dma_cycle(dma_cycle), .dma_addr(dma_addr),
        .dma_dout(dma_data), .dma_din(dma_result), .dma_we(dma_we), .irq_ext_n(!reu_irq),
        .joyA(7'h7F), .joyB(7'h7F), .pot1(8'hFF), .pot2(8'hFF),
        .pot3(8'hFF), .pot4(8'hFF), .audio_l(), .audio_r(),
        .sid_filter(2'b11), .sid_ver(2'b00), .sid_mode(3'b011), .sid_cfg(4'b0000),
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

    wire [23:0] reu_addr, memory_addr;
    wire reu_write, reu_valid, reu_ready, reu_rvalid;
    wire [63:0] reu_wdata, memory_wdata;
    wire [7:0] reu_wstrb, memory_wstrb;
    wire memory_write, memory_valid;
    logic memory_ready = 0, memory_rvalid = 0;
    logic [63:0] memory_rdata = 0;
    logic [7:0] expansion_ram [0:16777215];
    logic pending = 0, pending_write;
    logic [23:0] pending_addr;
    logic [63:0] pending_wdata;
    logic [7:0] pending_wstrb;
    integer response_delay = 0, backend_clocks = 0, requests = 0;
    integer dma_acks = 0, sid_dma_writes = 0, blocked_clocks = 0, irq_assertions = 0;
    logic last_irq = 0;
    logic [7:0] dma_floating [0:15];
    integer floating_reads = 0;

    multitini_c64_reu reu (
        .clk(clk), .reset(!reset_n), .enable(1'b1),
        .io_valid(cpu_bus_valid && iof && !dma_req), .io_addr(cpu_bus_addr[4:0]),
        .io_write(cpu_bus_write), .io_wdata(cpu_bus_wdata), .io_rdata(reu_io_data),
        .ff00_write(cpu_bus_valid && cpu_bus_write && cpu_bus_addr == 16'hff00),
        .dma_req(dma_req), .dma_valid(dma_valid), .dma_addr(dma_addr),
        .dma_wdata(dma_data), .dma_write(dma_we), .dma_ack(dma_ack), .dma_rdata(dma_result),
        .memory_addr(reu_addr), .memory_write(reu_write), .memory_wdata(reu_wdata),
        .memory_wstrb(reu_wstrb), .memory_valid(reu_valid), .memory_ready(reu_ready),
        .memory_rdata(memory_rdata), .memory_rvalid(reu_rvalid), .memory_fault(1'b0),
        .irq(reu_irq), .busy(), .fault(reu_fault)
    );
    multitini_c64_memory_arbiter arbiter (
        .clk(clk), .reset(!reset_n),
        .cart_addr(24'd0), .cart_write(1'b0), .cart_wdata(64'd0), .cart_wstrb(8'd0),
        .cart_valid(1'b0), .cart_ready(), .cart_rvalid(),
        .reu_addr(reu_addr), .reu_write(reu_write), .reu_wdata(reu_wdata), .reu_wstrb(reu_wstrb),
        .reu_valid(reu_valid), .reu_ready(reu_ready), .reu_rvalid(reu_rvalid),
        .memory_addr(memory_addr), .memory_write(memory_write), .memory_wdata(memory_wdata),
        .memory_wstrb(memory_wstrb), .memory_valid(memory_valid), .memory_ready(memory_ready),
        .memory_rvalid(memory_rvalid), .memory_fault(1'b0)
    );

    // Model a one-outstanding 64-bit PSRAM service with independent request
    // backpressure and response delay. Actual native slots must absorb this.
    always @(posedge clk) begin
        if (!reset_n) begin
            pending <= 0; memory_rvalid <= 0; memory_ready <= 0;
            backend_clocks <= 0; requests <= 0;
        end else begin
            backend_clocks <= backend_clocks + 1;
            memory_rvalid <= 0;
            memory_ready <= !pending && backend_clocks % 11 < 5;
            if (memory_valid && memory_ready) begin
                check(!pending, "one outstanding PSRAM transaction");
                pending <= 1; memory_ready <= 0;
                pending_addr <= memory_addr; pending_write <= memory_write;
                pending_wdata <= memory_wdata; pending_wstrb <= memory_wstrb;
                response_delay <= 13 + (requests % 7) * 47;
                requests <= requests + 1;
            end
            if (pending) begin
                if (response_delay > 0) response_delay <= response_delay - 1;
                else begin
                    for (integer lane=0; lane<8; lane++) begin
                        if (pending_write && pending_wstrb[lane])
                            expansion_ram[pending_addr + lane] = pending_wdata[lane*8 +: 8];
                        memory_rdata[lane*8 +: 8] <= expansion_ram[pending_addr + lane];
                    end
                    pending <= 0; memory_rvalid <= 1;
                end
            end
        end
    end

    task automatic check(input logic okay, input string message);
        if (okay !== 1'b1) $fatal(1, "FAIL: REU machine ntsc=%0d mode=%0d %s", ntsc, variant, message);
    endtask
    always @(posedge clk) begin
        if (running && !dbg_reset && !paused) begin
            check(!dbg_hold && !reu_fault, "REU waits never hold the machine clock");
            if (dbg_dma) begin
                check(!dbg_retire, "DMA ownership holds only CPU retirement");
                if (!dma_valid) begin
                    blocked_clocks++;
                    if (dbg_phase == 28) check(!ram_ce, "unready DMA does not issue machine RAM access");
                    check(!dbg_io_write, "unready DMA has no I/O write side effect");
                end
                if (!dbg_rdy) dma_checks++;
            end
            if (dma_ack) begin
                check(dbg_rdy, "REU waits for raw VIC BA as in MAME");
                dma_acks++;
                if (!dma_we && dma_addr == 16'hdf00) begin
                    check(floating_reads < 16 && dma_result === dbg_vic_bus,
                          "DMA from REU aperture reads VIC floating bus as in MAME");
                    dma_floating[floating_reads] = dma_result;
                    floating_reads++;
                end
            end
            if (dbg_io_write && dbg_io_cs[1]) sid_dma_writes++;
            if (reu_irq && !last_irq) irq_assertions++;
            last_irq = reu_irq;
            if (count_native) begin
                active_ticks++;
                if (dbg_native_ce[0]) begin
                    if (last_sid_tick >= 0) check(active_ticks-last_sid_tick == 32, "SID native clock through REU waits");
                    last_sid_tick = active_ticks; native_ticks++;
                end
                if (dbg_native_ce[1]) cia_p_ticks++;
                if (dbg_native_ce[2]) cia_n_ticks++;
                if (dbg_native_ce[3]) vic_ticks++;
                if (dbg_native_ce[4]) pixel_ticks++;
            end
            if (cpu_bus_valid && cpu_bus_write && cpu_bus_addr == 16'h2105 && cpu_bus_wdata == 8'ha5)
                finished = 1;
        end
    end

    task automatic run_case(input integer mode);
        variant = mode;
        @(negedge clk); running=0; count_native=0; reset_n=0; pause=1;
        repeat(160) @(negedge clk);
        loader_we=1;
        for(integer address=0; address<65536; address++) begin
            loader_addr=address; loader_data=initial_ram[address]; @(negedge clk);
        end
        loader_we=0;
        turbo_mode=mode ? 2'b10 : 2'b00; turbo_speed=mode ? 2'b11 : 2'b00;
        dma_acks=0; sid_dma_writes=0; blocked_clocks=0; dma_checks=0; irq_assertions=0; last_irq=0; floating_reads=0;
        native_ticks=0; cia_p_ticks=0; cia_n_ticks=0; vic_ticks=0; pixel_ticks=0;
        active_ticks=0; last_sid_tick=-1; finished=0; run_clocks=0;
        reset_n=1; pause=0; running=1;
        while (!finished && run_clocks<1200000) begin
            @(negedge clk); run_clocks++;
            if (!dbg_reset && dbg_phase==0) count_native=1;
        end
        check(finished, "CPU finishes actual REU register command program");
        check(dma_acks==1568, $sformatf("exact accepted DMA transactions expected1568 got%0d",dma_acks));
        check(sid_dma_writes==16, "PSRAM stalls never duplicate DMA SID writes");
        check(blocked_clocks>10000 && dma_checks>0, "delayed PSRAM and real VIC bus steals exercised");
        check(irq_assertions==4 && floating_reads==16, "REU completion IRQs and floating-bus aperture reads");
        for(integer i=0;i<4;i++) check(memory_i.ram[16'h2100+i]===8'hd0,"CPU reads MAME completion/IRQ status");
        for(integer i=0;i<768;i++) begin
            check(memory_i.ram[16'h4800+i]===initial_ram[16'h4000+i], "REU high-bank round-trip data");
            check(expansion_ram[(24'hfffe80+i)&24'hffffff]===
                  (i<16 ? dma_floating[i] : initial_ram[16'h4000+i]),"24-bit REU wrap and floating-bus data");
        end
        check(cia_p_ticks>=native_ticks-1 && cia_p_ticks<=native_ticks+1 &&
              cia_n_ticks>=native_ticks-1 && cia_n_ticks<=native_ticks+1,"native CIA cadence");
        check(vic_ticks>=native_ticks*2-2 && vic_ticks<=native_ticks*2+2 &&
              pixel_ticks>=native_ticks*8-8 && pixel_ticks<=native_ticks*8+8,"native VIC data/pixel cadence");
        $display("REU_MACHINE_SUMMARY ntsc=%0d mode=%0d dma=%0d waits=%0d vic_held=%0d sid_writes=%0d clocks=%0d",
                 ntsc,mode,dma_acks,blocked_clocks,dma_checks,sid_dma_writes,run_clocks);
        running=0; count_native=0;
    endtask
    initial begin
        $readmemh("machine_memory.hex",initial_ram);
        load_roms();
        for(integer standard=0;standard<2;standard++) begin
            ntsc=standard;
            for(integer mode=0;mode<2;mode++) run_case(mode);
        end
        $display("PASS: C64 production REU machine PAL/NTSC stock/dense, native VIC/CIA/SID, 24-bit wrap, DMA I/O and IRQ");
        $finish;
    end
    initial begin #60000000; $fatal(1,"FAIL: REU machine timeout"); end
endmodule


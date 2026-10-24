// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Two complete production machines receive identical synthetic memories and
// external signals. Lane zero retains its native CPU. Lane one uses the real
// external island, proxy, split memory and fast ports, with exact policy.
module tb_c64_turbo_exact;
    reg clk=0,fastclk=0,reset_n=0,pause=1,memory_wait=0,ntsc=0,irq_n=1,nmi_n=1;
    always #5 clk=~clk;
    // First CPU rising edge lies a quarter native cycle after a native edge.
    initial begin #1.25; fastclk=1; forever #2.5 fastclk=~fastclk; end
    reg [1:0]bios=0;
    reg rom_we=0,loader_we=0;
    reg[13:0]rom_addr=0;
    reg[7:0]rom_data=0,loader_data=0;
    reg[15:0]loader_addr=0;
    reg dma_req=0,dma_we=0;
    reg[15:0]dma_addr=16'h4000;
    reg[7:0]dma_data=8'h76;
    wire[7:0]dma_result[2],ram_data[2],ram_q[2];
    wire dma_cycle[2],ram_ce[2],ram_we[2],paused[2];
    wire[15:0]ram_addr[2],dbg_cpu_addr[2];
    wire[4:0]dbg_phase[2],dbg_io_cs[2];
    wire[7:0]dbg_cpu_di[2],dbg_cpu_do[2],dbg_cpu_port[2],dbg_vic_bus[2];
    wire dbg_cpu_we[2],dbg_request[2],dbg_retire[2],dbg_reset[2],dbg_hold[2];
    wire dbg_rdy[2],dbg_cpu_owned[2],dbg_dma[2],dbg_io_write[2];
    wire[5:0]dbg_native_ce[2];
    wire hsync[2],vsync[2],iec_data[2],iec_clk[2],iec_atn[2];
    wire[7:0]red[2],green[2],blue[2],pb_o[2],pb_oe[2];
    wire[17:0]audio_l[2],audio_r[2];
    reg[7:0]initial_ram[0:65535];
    integer checks=0,retired=0,io_checks=0,mem_checks=0,ticks=0,ba_ticks=0,dma_checks=0,interrupt_checks=0;
    integer trace_file,failures=0,first_bad=-1;
    reg comparing=0,started=0,finished=0,setup_ready=0;
    for(genvar g=0;g<2;g=g+1)begin: lane
        c64_turbo_exact_lane #(.EXTERNAL(g)) dut(
            .clk(clk),.fastclk(fastclk),.reset_n(reset_n),.pause(pause),.memory_wait(memory_wait),
            .ntsc(ntsc),.irq_n(irq_n),.nmi_n(nmi_n),.bios(bios),.rom_we(rom_we),.loader_we(loader_we),
            .rom_addr(rom_addr),.rom_data(rom_data),.loader_data(loader_data),.loader_addr(loader_addr),
            .dma_req(dma_req),.dma_we(dma_we),.dma_addr(dma_addr),.dma_data(dma_data),
            .dma_result(dma_result[g]),.dma_cycle(dma_cycle[g]),.ram_addr(ram_addr[g]),
            .ram_data(ram_data[g]),.ram_q(ram_q[g]),.ram_ce(ram_ce[g]),.ram_we(ram_we[g]),.paused(paused[g]),
            .dbg_phase(dbg_phase[g]),.dbg_cpu_addr(dbg_cpu_addr[g]),.dbg_cpu_di(dbg_cpu_di[g]),
            .dbg_cpu_do(dbg_cpu_do[g]),.dbg_cpu_port(dbg_cpu_port[g]),.dbg_vic_bus(dbg_vic_bus[g]),
            .dbg_cpu_we(dbg_cpu_we[g]),.dbg_request(dbg_request[g]),.dbg_retire(dbg_retire[g]),
            .dbg_reset(dbg_reset[g]),.dbg_hold(dbg_hold[g]),.dbg_rdy(dbg_rdy[g]),
            .dbg_cpu_owned(dbg_cpu_owned[g]),.dbg_dma(dbg_dma[g]),.dbg_native_ce(dbg_native_ce[g]),
            .dbg_io_cs(dbg_io_cs[g]),.dbg_io_write(dbg_io_write[g]),.hsync(hsync[g]),.vsync(vsync[g]),
            .red(red[g]),.green(green[g]),.blue(blue[g]),.audio_l(audio_l[g]),.audio_r(audio_r[g]),
            .iec_data(iec_data[g]),.iec_clk(iec_clk[g]),.iec_atn(iec_atn[g]),.pb_o(pb_o[g]),.pb_oe(pb_oe[g]));
    end
    task automatic check(input logic okay,input string label);
        checks++;
        if(okay!==1'b1)begin
            if(first_bad<0)first_bad=ticks;
            failures++;
            if(failures<=12)$display("MISMATCH tick=%0d phase=%0d %s",ticks,dbg_phase[0],label);
            $fatal(1,"FAIL: exact machine lockstep or required coverage diverged");
        end
    endtask
    always @(posedge clk)begin
        if(comparing)begin
            ticks++;
            check({dbg_phase[0],dbg_reset[0],dbg_hold[0],paused[0],dbg_native_ce[0]}===
                  {dbg_phase[1],dbg_reset[1],dbg_hold[1],paused[1],dbg_native_ce[1]},"native scheduler");
            if(!dbg_reset[0])begin
                if(!dbg_rdy[0])ba_ticks++;
                if(dbg_dma[0])dma_checks++;
                check({ram_ce[0],ram_we[0],dbg_rdy[0],dbg_cpu_owned[0],dbg_dma[0]}===
                      {ram_ce[1],ram_we[1],dbg_rdy[1],dbg_cpu_owned[1],dbg_dma[1]},"bus ownership/strobes");
                if(ram_ce[0])begin
                    mem_checks++;
                    check({ram_addr[0],ram_we[0]}==={ram_addr[1],ram_we[1]},
                        $sformatf("RAM address native=%h external=%h",ram_addr[0],ram_addr[1]));
                    if(ram_we[0])check(ram_data[0]===ram_data[1],"RAM write byte");
                end
                if(dbg_request[0]&&dbg_cpu_owned[0]&&!dbg_dma[0])begin
                    check({dbg_cpu_addr[0],dbg_cpu_we[0]}==={dbg_cpu_addr[1],dbg_cpu_we[1]},
                        $sformatf("CPU issue native=%h external=%h",dbg_cpu_addr[0],dbg_cpu_addr[1]));
                    if(dbg_cpu_we[0])check(dbg_cpu_do[0]===dbg_cpu_do[1],"CPU write byte");
                end
                if(dbg_retire[0])begin
                    retired++;
                    $fdisplay(trace_file,"%0d,%0d,%04x,%01x,%02x,%04x,%01x,%02x",ntsc,ticks,
                        dbg_cpu_addr[0],dbg_cpu_we[0],dbg_cpu_we[0]?dbg_cpu_do[0]:dbg_cpu_di[0],
                        dbg_cpu_addr[1],dbg_cpu_we[1],dbg_cpu_we[1]?dbg_cpu_do[1]:dbg_cpu_di[1]);
                    check(dbg_retire[1],"CPU retirement opportunity");
                    // $00/$01 are consumed inside each real 6510 port. Their
                    // backing RAM is compared above, and output pins below.
                    if(dbg_cpu_addr[0]>1 && !dbg_cpu_we[0])
                        check(dbg_cpu_di[0]===dbg_cpu_di[1],"CPU read byte");
                    check(dbg_cpu_port[0]===dbg_cpu_port[1],"6510 physical port pins");
                    if(dbg_cpu_we[0]&&dbg_cpu_addr[0]==16'h2105&&dbg_cpu_do[0]==8'ha5)finished=1;
                    if(dbg_cpu_we[0]&&dbg_cpu_addr[0]==16'h2109&&dbg_cpu_do[0]==8'h5a)setup_ready=1;
                    if(dbg_cpu_we[0]&&dbg_cpu_addr[0]==16'h2200)interrupt_checks++;
                end
                if(dbg_io_write[0]||dbg_io_write[1])begin
                    io_checks++;
                    check({dbg_io_cs[0],dbg_io_write[0]}==={dbg_io_cs[1],dbg_io_write[1]},"I/O selection and side-effect pulse");
                end
                check({hsync[0],vsync[0],red[0],green[0],blue[0],audio_l[0],audio_r[0]}===
                      {hsync[1],vsync[1],red[1],green[1],blue[1],audio_l[1],audio_r[1]},"pixel and audio output");
                check({iec_data[0],iec_clk[0],iec_atn[0],pb_o[0],pb_oe[0]}===
                      {iec_data[1],iec_clk[1],iec_atn[1],pb_o[1],pb_oe[1]},"CIA external pins");
            end
        end
    end
    task automatic load_all;
        @(negedge clk);loader_we=1;
        for(integer a=0;a<65536;a++)begin loader_addr=a;loader_data=initial_ram[a];@(negedge clk);end
        loader_we=0;bios=0;rom_we=1;
        for(integer a=0;a<16384;a++)begin
            rom_addr=a;rom_data=a == 0 ? 8'h42 : a == 14'h3ffc ? 8'h00 : a == 14'h3ffd ? 8'h08 :
                (a == 14'h3ffa || a == 14'h3ffe) ? 8'h00 : (a == 14'h3ffb || a == 14'h3fff) ? 8'h0b : 8'hea;
            @(negedge clk);
        end
        bios=1;
        for(integer a=0;a<4096;a++)begin rom_addr=a;rom_data=8'hc7;@(negedge clk);end
        rom_we=0;bios=0;
    endtask
    task automatic dma_transfer(input reg wr,input reg[15:0]address,input reg[7:0]value);
        integer timeout_ticks;
        @(negedge clk);dma_req=1;dma_we=wr;dma_addr=address;dma_data=value;
        timeout_ticks=0;
        while(!(dma_cycle[0] && dbg_phase[0]==28) && timeout_ticks<4096)begin
            @(negedge clk);timeout_ticks++;
        end
        check(timeout_ticks<4096,"DMA obtained native slot");
        check(dma_cycle[1],"external machine has same DMA ownership");
        @(negedge clk);while(dbg_phase[0]!=31)@(negedge clk);
        if(!wr)check(dma_result[0]===dma_result[1],"DMA read result");
        dma_req=0;dma_we=0;
        while(dbg_dma[0])@(negedge clk);
    endtask
    initial begin
        $readmemh("machine_memory.hex",initial_ram);
        trace_file=$fopen("exact_trace.csv","w");if(!trace_file)$fatal(1,"trace open failed");
        $fdisplay(trace_file,"ntsc,tick,native_addr,native_we,native_data,external_addr,external_we,external_data");
        for(integer mode=0;mode<2;mode++)begin
            comparing=0;reset_n=0;pause=1;ntsc=mode;finished=0;setup_ready=0;
            load_all();repeat(64)@(negedge clk);
            comparing=1;reset_n=1;pause=0;
            begin
                integer setup_ticks;setup_ticks=0;
                while(!setup_ready&&setup_ticks<400000)begin @(negedge clk);setup_ticks++;end
                check(setup_ready,"display, SID and CLI initialization completed before interrupts");
            end
            check(finished,"synthetic program reached result marker");
            // Preserve each in-flight phase and verify coherent resume.
            wait(dbg_phase[0]==31);@(negedge clk);memory_wait=1;
            repeat(9)@(negedge clk);memory_wait=0;
            wait(dbg_phase[0]==15);@(negedge clk);memory_wait=1;
            repeat(7)@(negedge clk);memory_wait=0;
            begin
                integer pause_ticks;pause_ticks=0;pause=1;
                // The native core accepts pause only in one of four refresh
                // frames; do not assume the phase of the request.
                while(!(paused[0]&&paused[1])&&pause_ticks<256)begin @(negedge clk);pause_ticks++;end
                check(paused[0]&&paused[1],"pause acknowledged");
                repeat(30)@(negedge clk);pause=0;
            end
            dma_transfer(1,16'h4000,8'h76);dma_transfer(0,16'h4000,0);
            dma_transfer(1,16'h0000,8'h24);dma_transfer(0,16'h0000,0);
            dma_transfer(1,16'h0001,8'h51);dma_transfer(0,16'h0001,0);
            @(negedge clk);nmi_n=0;repeat(40)@(negedge clk);nmi_n=1;
            // Keep IRQ asserted beyond an entire badline/sprite steal. The
            // first short pulse deliberately used by the bring-up bench can
            // be hidden completely by VIC ownership and is not IRQ coverage.
            repeat(2000)@(negedge clk);irq_n=0;repeat(4000)@(negedge clk);irq_n=1;
            repeat(mode?1095000:1259000)@(negedge clk);
            check(failures==0,"no accumulated divergence");
            check(ba_ticks>1000 && dma_checks>100 && interrupt_checks>=4*(mode+1),"VIC steals, DMA and both interrupts exercised");
            $display("EXACT_SUMMARY ntsc=%0d checks=%0d retired=%0d memory=%0d io=%0d ba=%0d dma=%0d interrupt_writes=%0d",mode,checks,retired,mem_checks,io_checks,ba_ticks,dma_checks,interrupt_checks);
        end
        $fclose(trace_file);
        if(failures!=0)$fatal(1,"FAIL: exact lockstep accumulated %0d failures",failures);
        $display("PASS: exact external CPU full-machine lockstep PAL/NTSC");$finish;
    end
    initial begin #40000000;$fatal(1,"FAIL: bounded exact machine timeout");end
endmodule

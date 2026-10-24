// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// One complete production machine uses the actual island/proxy, CIA timers,
// VIC bus steals and landed fast SID writes. Index 15, D031=8F and 0F.
module tb_c64_turbo_digi;
    reg clk=0,fastclk=0,reset_n=0,pause=1,memory_wait=0,ntsc=0,irq_n=1,nmi_n=1;
    // One generator, exact 2:1 clocks with coincident rising edges.
    initial forever begin
        #(ntsc?7.638:7.9295);fastclk=1;clk=1;
        #(ntsc?7.638:7.9295);fastclk=0;
        #(ntsc?7.638:7.9295);fastclk=1;clk=0;
        #(ntsc?7.638:7.9295);fastclk=0;
    end
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
    reg [3:0] rate=15;reg badline=1;integer irq_mode=0;
    wire digi_sid_wr,digi_badline;wire[9:0]digi_sid_a;wire[7:0]digi_sid_d,digi_sprite_dma;
    for(genvar g=1;g<2;g=g+1)begin: lane
        c64_turbo_exact_lane #(.EXTERNAL(g)) dut(
            .test_config(32'h420),.digi_sid_wr(digi_sid_wr),.digi_sid_a(digi_sid_a),.digi_sid_d(digi_sid_d),.digi_badline(digi_badline),.digi_sprite_dma(digi_sprite_dma),
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


    integer fast_ticks=0,samples=0,last_sid=0,min_interval=32'h7fffffff,max_interval=0;
    integer ba_low_ticks=0,io_low_acks=0,sid_low_writes=0,cpu_low_enables=0;
    integer first_sid=0,cia_acks=0,timer_edges=0,badline_ticks=0;
    reg [7:0] sprite_dma_seen=0;
    reg previous_interrupt=1;
    wire timer_interrupt=irq_mode?lane[1].dut.ext_irq_n:lane[1].dut.ext_nmi_n;
    integer rows_file,summary_file,cases=0,measure_ticks=0;
    reg measuring=0,ready=0;
    always @(posedge fastclk)begin
        fast_ticks++;
        if(lane[1].dut.accelerated.cpu_island_i.cpu_enable &&
           lane[1].dut.accelerated.cpu_island_i.cpu_write &&
           lane[1].dut.accelerated.cpu_island_i.cpu_address==16'h2105)ready=1;
        if(measuring)begin
            measure_ticks++;
            if(!dbg_rdy[1])ba_low_ticks++;
            if(lane[1].dut.accelerated.cpu_island_i.cpu_enable && !dbg_rdy[1])cpu_low_enables++;
        end
    end
    always @(posedge clk)begin
        previous_interrupt<=timer_interrupt;
        if(measuring)begin
            if(previous_interrupt && !timer_interrupt)timer_edges++;
            sprite_dma_seen=sprite_dma_seen|digi_sprite_dma;
            if(digi_badline)badline_ticks++;
            if(lane[1].dut.ext_cpu_bus_valid && !lane[1].dut.ext_cpu_we &&
                lane[1].dut.ext_cpu_addr==(irq_mode?16'hdc0d:16'hdd0d))begin
                cia_acks++;
                if(!dbg_rdy[1])io_low_acks++;
            end
            if(digi_sid_wr && digi_sid_a[4:0]==5'h18)begin
                integer interval;
                interval=fast_ticks-last_sid;
                if(samples>2)begin
                    if(interval<min_interval)min_interval=interval;
                    if(interval>max_interval)max_interval=interval;
                end
                if(!dbg_rdy[1])sid_low_writes++;
                $fdisplay(rows_file,"%0d,%0d,%0d,%0d,%0d,%0d,%0d",ntsc,irq_mode,badline,samples,fast_ticks,interval,dbg_rdy[1]);
                if(samples==0)first_sid=fast_ticks;last_sid=fast_ticks;samples++;
            end
        end
    end
    task automatic load_all;
        @(negedge clk);loader_we=1;
        for(integer a=0;a<65536;a++)begin loader_addr=a;loader_data=initial_ram[a];@(negedge clk);end
        loader_we=0;bios=0;rom_we=1;
        for(integer a=0;a<16384;a++)begin
            rom_addr=a;rom_data=a==14'h3ffc ? 8'h00 : a==14'h3ffd ? 8'h08 : 8'hea;@(negedge clk);
        end
        bios=1;
        for(integer a=0;a<4096;a++)begin rom_addr=a;rom_data=8'hff;@(negedge clk);end
        rom_we=0;bios=0;
    endtask
    initial begin
        string file_name;integer timeout_ticks,expected_period,expected_samples,span_error;
        rows_file=$fopen("digi_intervals.csv","w");summary_file=$fopen("digi_summary.csv","w");
        $fdisplay(rows_file,"ntsc,irq,badline,sample,fast_tick,interval,ba");
        $fdisplay(summary_file,"ntsc,irq,badline,samples,min_interval,max_interval,variation,ba_low_ticks,io_low_acks,sid_low_writes,cpu_low_enables,cia_acks,timer_edges,badline_ticks,sprite_dma_seen,expected_period,span_error");
        for(integer standard=0;standard<2;standard++)for(integer kind=0;kind<2;kind++)for(integer bad=0;bad<2;bad++)begin
            measuring=0;ready=0;reset_n=0;pause=1;ntsc=standard;irq_mode=kind;badline=bad;
            file_name=$sformatf("digi-%0d-%0d-%0d.hex",standard,kind,bad);$readmemh(file_name,initial_ram);load_all();
            repeat(160)@(negedge clk);reset_n=1;pause=0;timeout_ticks=0;
            while(!ready&&timeout_ticks<200000)begin @(negedge clk);timeout_ticks++;end
            if(!ready)$fatal(1,"Digi setup timeout");
            wait(lane[1].dut.island_turbo);repeat(128)@(negedge clk);
            if(lane[1].dut.accelerated.cpu_island_i.active_badline!==badline ||
               lane[1].dut.island_active_index!==4'd15 ||
               lane[1].dut.turbo_regs_i.live_d031!==(badline?8'h0f:8'h8f))$fatal(1,"D031 index15 policy not adopted");
            expected_period=(standard?128:123)*64;
            expected_samples=(standard?2*65*263:2*63*312)/(standard?128:123);
            samples=0;last_sid=fast_ticks;min_interval=32'h7fffffff;max_interval=0;
            ba_low_ticks=0;io_low_acks=0;sid_low_writes=0;cpu_low_enables=0;measure_ticks=0;
            first_sid=0;cia_acks=0;timer_edges=0;badline_ticks=0;sprite_dma_seen=0;measuring=1;
            // Two complete native video frames, independent of CPU speed.
            repeat(standard?2*65*263*32:2*63*312*32)@(negedge clk);
            measuring=0;
            span_error=(last_sid-first_sid)-(samples-1)*expected_period;
            $fdisplay(summary_file,"%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d",ntsc,irq_mode,badline,samples,min_interval,max_interval,max_interval-min_interval,ba_low_ticks,io_low_acks,sid_low_writes,cpu_low_enables,cia_acks,timer_edges,badline_ticks,sprite_dma_seen,expected_period,span_error);
            $display("DIGI_SUMMARY ntsc=%0d irq=%0d badline=%0d samples=%0d min=%0d max=%0d variation=%0d ba=%0d io_low=%0d sid_low=%0d cpu_low=%0d",ntsc,irq_mode,badline,samples,min_interval,max_interval,max_interval-min_interval,ba_low_ticks,io_low_acks,sid_low_writes,cpu_low_enables);
            if(samples<200||ba_low_ticks<10000||badline_ticks<10000||sprite_dma_seen!==8'hff)$fatal(1,"Digi insufficient timer/VIC exposure");
            if(samples<expected_samples-1||samples>expected_samples+1||
               cia_acks<samples-1||cia_acks>samples+1||timer_edges<samples-1||timer_edges>samples+1)
                $fatal(1,"Digi lost/duplicated timer event or acknowledge");
            if(!badline && (span_error>64||span_error< -64))$fatal(1,"Digi wrong sustained timer cadence");
            if(!badline && (max_interval-min_interval>64 || io_low_acks==0 || sid_low_writes==0 || cpu_low_enables==0))
                $fatal(1,"Digi D031=8F bound or BA-low forward failed");
            if(badline && max_interval-min_interval<=64)$fatal(1,"Digi D031=0F did not detect expected stalls");
            cases++;
        end
        $fclose(rows_file);$fclose(summary_file);$display("TURBO_DIGI PASS cases=%0d",cases);$finish;
    end
    initial begin #1000000000;$fatal(1,"Digi global timeout");end
endmodule


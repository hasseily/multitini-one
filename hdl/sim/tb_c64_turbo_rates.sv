// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Production island/proxy/FIO and complete native machine. Measured unrolled
// read and RAM-operation rates at every acceleration index, PAL and NTSC.
module tb_c64_turbo_rates;
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
    for(genvar g=1;g<2;g=g+1)begin: lane
        c64_turbo_exact_lane #(.EXTERNAL(g)) dut(
            .test_config(32'h410 | {28'd0,rate}),
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



    integer fast_ticks=0,enable_count=0,groups,fixture_file,parsed,trace_file,cases=0;
    integer start_tick[16],end_tick[16],start_count[16],end_count[16];
    reg [15:0] group_start[16],group_end[16];
    reg [255:0] group_name[16];
    integer de_count=0,de_first=0,de_last=0,de_min=32'h7fffffff,de_max=0;
    reg measuring=0,done=0;
    always @(posedge fastclk)begin
        fast_ticks++;
        if(measuring && lane[1].dut.accelerated.cpu_island_i.cpu_enable)begin
            enable_count++;
            if(!lane[1].dut.accelerated.cpu_island_i.cpu_write)begin
                for(integer g=0;g<groups;g++)begin
                    if(lane[1].dut.accelerated.cpu_island_i.cpu_address==group_start[g] && start_tick[g]<0)begin
                        start_tick[g]=fast_ticks;start_count[g]=enable_count;
                        if(!lane[1].dut.island_turbo || lane[1].dut.island_active_index!=rate || lane[1].dut.island_badline)
                            $fatal(1,"rate policy not adopted");
                    end
                    if(lane[1].dut.accelerated.cpu_island_i.cpu_address==group_end[g] && start_tick[g]>=0 && end_tick[g]<0)begin
                        end_tick[g]=fast_ticks;end_count[g]=enable_count;
                    end
                end
                if(lane[1].dut.accelerated.cpu_island_i.cpu_address==16'hde80)begin
                    integer delta;delta=fast_ticks-de_last;
                    if(de_count>0)begin if(delta<de_min)de_min=delta;if(delta>de_max)de_max=delta;end
                    if(de_count==0)de_first=fast_ticks;de_last=fast_ticks;de_count++;
                end
                if(lane[1].dut.accelerated.cpu_island_i.cpu_address==16'hd01e && start_tick[4]>=0 && end_tick[4]<0 &&
                   lane[1].dut.accelerated.cpu_island_i.cpu_di!==8'd0)$fatal(1,"rate zero-collision fixture not clear");
            end else if(lane[1].dut.accelerated.cpu_island_i.cpu_address==16'h2105)done=1;
        end
    end
    task automatic load_all;
        @(negedge clk);loader_we=1;
        for(integer a=0;a<65536;a++)begin loader_addr=a;loader_data=initial_ram[a];@(negedge clk);end
        loader_we=0;bios=0;rom_we=1;
        for(integer a=0;a<16384;a++)begin
            rom_addr=a;rom_data=a==14'h3ffc ? 8'h00 : a==14'h3ffd ? 8'h07 : 8'hea;@(negedge clk);
        end
        bios=1;
        for(integer a=0;a<4096;a++)begin rom_addr=a;rom_data=8'hc7;@(negedge clk);end
        rom_we=0;bios=0;
    endtask
    initial begin
        integer timeout_ticks;
        $readmemh("rate_memory.hex",initial_ram);
        fixture_file=$fopen("rate_groups.txt","r");parsed=$fscanf(fixture_file,"%d",groups);
        if(parsed!=1||groups!=10)$fatal(1,"missing rate groups");
        for(integer g=0;g<groups;g++)begin parsed=$fscanf(fixture_file,"%s %h %h",group_name[g],group_start[g],group_end[g]);if(parsed!=3)$fatal(1,"bad rate group");end
        $fclose(fixture_file);
        trace_file=$fopen("rates.csv","w");$fdisplay(trace_file,"ntsc,index,group,fast_clocks,cpu_enables,de_reads,de_span,de_min,de_max");
        for(integer standard=0;standard<2;standard++)begin
            ntsc=standard;reset_n=0;pause=1;load_all();
            for(integer index=0;index<16;index++)begin
                measuring=0;done=0;reset_n=0;pause=1;rate=index;
                repeat(160)@(negedge clk);
                loader_we=1;loader_addr=16'h0020;loader_data=0;@(negedge clk);
                loader_addr=16'h2000;@(negedge clk);loader_we=0;
                for(integer g=0;g<groups;g++)begin start_tick[g]=-1;end_tick[g]=-1;start_count[g]=0;end_count[g]=0;end
                de_count=0;de_first=0;de_last=0;de_min=32'h7fffffff;de_max=0;enable_count=0;
                measuring=1;reset_n=1;pause=0;timeout_ticks=0;
                while(!done&&timeout_ticks<200000)begin @(negedge clk);timeout_ticks++;end
                measuring=0;
                if(!done)$fatal(1,"rate timeout index=%0d addr=%x",index,lane[1].dut.accelerated.cpu_island_i.cpu_address);
                if(de_count!=32)$fatal(1,"forward sample count");
                if(lane[1].dut.accelerated.main_ram_i.main_low[16'h20]!==8'd32 ||
                   lane[1].dut.accelerated.main_ram_i.main_low[16'h2000]!==8'd32 ||
                   lane[1].dut.accelerated.main_ram_i.main_low[16'h3000]!==8'd32)$fatal(1,"rate instruction result mismatch");
                for(integer g=0;g<groups;g++)begin
                    if(start_tick[g]<0||end_tick[g]<start_tick[g])$fatal(1,"missing rate group");
                    $fdisplay(trace_file,"%0d,%0d,%s,%0d,%0d,%0d,%0d,%0d,%0d",ntsc,index,group_name[g],end_tick[g]-start_tick[g],end_count[g]-start_count[g],de_count,de_last-de_first,de_min,de_max);
                end
                $display("RATE_SUMMARY ntsc=%0d index=%0d de_min=%0d de_max=%0d",ntsc,index,de_min,de_max);cases++;
            end
        end
        $fclose(trace_file);$display("TURBO_RATES PASS cases=%0d groups=%0d",cases,groups);$finish;
    end
    initial begin #1000000000;$fatal(1,"rate global timeout");end
endmodule

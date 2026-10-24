// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Derived from tb_c64_turbo_ordering: real native and island machines.
// Check the Store_Port contract at the actual falling-edge BRAM write.
// Read-cycle memory data is deliberately outside this helper contract.
module tb_c64_cpu_write_path;
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
    reg [3:0] rate=0;reg badline=1;
    for(genvar g=0;g<2;g=g+1)begin: lane
        c64_turbo_exact_lane #(.EXTERNAL(g)) dut(
            .test_config(g && setup_ready ? (32'h410 | {28'd0,rate} | (badline?32'h40:0)) : 32'd0),
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

    reg [24:0] bus_trace[2][8192];
    integer trace_length[2];reg[1:0]trace_started=0,trace_finished=0;
    integer cases=0,total_cycles=0,fio_entries_seen=0;
    reg [1:0] warmed=0;
    task automatic record(input integer g,input reg enabled,input reg[15:0]address,input reg wr,input reg[7:0]d);
        reg[7:0]sample;
        if(comparing&&enabled&&!trace_finished[g])begin
            if(wr&&address==16'h2108&&d==8'ha7)warmed[g]=1;
            if(!wr&&address==16'h0800)trace_started[g]=1;
            if(trace_started[g])begin
                sample=d;
                if(!wr&&address==16'hd800)sample=d&8'h0f;
                if(trace_length[g]>=8192)$fatal(1,"bounded ordering trace");
                bus_trace[g][trace_length[g]]={address,wr,sample};trace_length[g]++;
                if(wr&&address==16'h2105&&d==8'ha5)trace_finished[g]=1;
            end
        end
    endtask
    always @(posedge clk)begin
        record(0,dbg_retire[0]&&(dbg_rdy[0]||dbg_cpu_we[0]),dbg_cpu_addr[0],dbg_cpu_we[0],dbg_cpu_we[0]?dbg_cpu_do[0]:dbg_cpu_di[0]);
        if(comparing&&!dbg_reset[1])begin
            if(lane[1].dut.cpu_status[20]!==paused[1])$fatal(1,"CPU status must report actual pause acknowledgement");
            if(lane[1].dut.fio_u64_wr)fio_entries_seen++;
        end
    end
    always @(posedge fastclk)begin
        if(comparing&&!lane[1].dut.accelerated.cpu_island_i.reset_q&&$isunknown(lane[1].dut.accelerated.cpu_island_i.wp_next))
            $fatal(1,"Unknown FIO push addr=%h wr=%b turbo=%b state=%0d base=%b fio=%b push=%b busy=%b wp=%b q=%h",lane[1].dut.accelerated.cpu_island_i.cpu_address,lane[1].dut.accelerated.cpu_island_i.cpu_write,lane[1].dut.island_turbo,lane[1].dut.accelerated.cpu_island_i.state,lane[1].dut.accelerated.cpu_island_i.base,lane[1].dut.accelerated.cpu_island_i.fio_ok,lane[1].dut.accelerated.cpu_island_i.push,lane[1].dut.accelerated.cpu_island_i.busy_q,lane[1].dut.fio_wp,lane[1].dut.accelerated.cpu_island_i.io2_q);
        record(1,lane[1].dut.accelerated.cpu_island_i.cpu_enable &&
            (lane[1].dut.island_turbo||lane[1].dut.b0_rdy||lane[1].dut.accelerated.cpu_island_i.cpu_write),
            lane[1].dut.accelerated.cpu_island_i.cpu_address,
            lane[1].dut.accelerated.cpu_island_i.cpu_write,
            lane[1].dut.accelerated.cpu_island_i.cpu_write ? lane[1].dut.accelerated.cpu_island_i.cpu_wdata :
            lane[1].dut.accelerated.cpu_island_i.observe_cpu_di);
    end

    wire [15:0] checked_address=lane[1].dut.accelerated.cpu_island_i.cpu_address;
    wire checked_write=lane[1].dut.accelerated.cpu_island_i.cpu_write;
    wire checked_helper=lane[1].dut.accelerated.cpu_island_i.cpu_store_port;
    wire checked_enable=lane[1].dut.accelerated.cpu_island_i.cpu_enable;
    wire checked_reset=lane[1].dut.accelerated.cpu_island_i.reset_q;
    wire checked_ce=lane[1].dut.accelerated.cpu_island_i.cpu_mem_ce;
    wire checked_ram_we=lane[1].dut.accelerated.cpu_island_i.cpu_mem_ram_we;
    wire checked_colour_we=lane[1].dut.accelerated.cpu_island_i.cpu_mem_colour_we;
    wire [7:0] checked_data=lane[1].dut.accelerated.cpu_island_i.cpu_mem_wdata;
    wire [7:0] expected_data=checked_address[15:1]==0 ?
        lane[1].dut.accelerated.cpu_island_i.last_vic_q :
        lane[1].dut.accelerated.cpu_island_i.cpu_wdata;
    integer fall_port0=0,fall_port1=0,fall_regular=0,fall_colour=0,disabled_writes=0,retired_stack=0;
    integer retired_port0=0,retired_port1=0;
    wire exact_policy=badline&&rate==0;
    always @(negedge fastclk)begin
        reg [7:0] observed;
        if(!reset_n)begin fall_port0=0;fall_port1=0;fall_regular=0;fall_colour=0;disabled_writes=0;end
        else if(!checked_reset)begin
            if(checked_write&&checked_helper!==(checked_address[15:1]==0))
                $fatal(1,"write helper mismatch address=%h helper=%b",checked_address,checked_helper);
            if((checked_ram_we||checked_colour_we)&&!checked_ce)$fatal(1,"disabled memory write");
            if(checked_ce&&(checked_ram_we||checked_colour_we))begin
                observed=checked_data;
                if($test$plusargs("mutate_write")&&checked_address<2)observed=observed^8'h01;
                if($isunknown(expected_data)||observed!==expected_data)
                    $fatal(1,"WRITE_PATH_MUTATION_REJECT address=%h data=%h expected=%h",checked_address,observed,expected_data);
                if(checked_colour_we)fall_colour++;
                else if(checked_address==0)fall_port0++;
                else if(checked_address==1)fall_port1++;
                else fall_regular++;
            end else if(checked_write)disabled_writes++;
        end else if(checked_ram_we||checked_colour_we)$fatal(1,"memory write during reset");
    end
    always @(posedge fastclk)begin
        if(!reset_n)begin retired_stack=0;retired_port0=0;retired_port1=0;end
        else if(!checked_reset&&checked_enable&&checked_write)begin
            if(checked_helper!==(checked_address[15:1]==0))$fatal(1,"retired helper mismatch");
            if(checked_address[15:8]==8'h01)retired_stack++;
            if(trace_started[1]&&checked_address==0)retired_port0++;
            if(trace_started[1]&&checked_address==1)retired_port1++;
        end
        if(reset_n&&setup_ready&&exact_policy&&lane[1].dut.island_turbo)
            $fatal(1,"index0 with badline policy must remain exact");
    end

    task automatic load_all;
        @(negedge clk);loader_we=1;
        for(integer a=0;a<65536;a++)begin loader_addr=a;loader_data=initial_ram[a];@(negedge clk);end
        loader_we=0;bios=0;rom_we=1;
        for(integer a=0;a<16384;a++)begin
            rom_addr=a;rom_data=a==0 ? 8'h42 : a==14'h3ffc ? 8'h00 : a==14'h3ffd ? 8'h40 :
                (a==14'h3ffa||a==14'h3ffe)?8'h00:(a==14'h3ffb||a==14'h3fff)?8'h07:8'hea;
            @(negedge clk);
        end
        bios=1;
        for(integer a=0;a<4096;a++)begin rom_addr=a;rom_data=8'hc7;@(negedge clk);end
        rom_we=0;bios=0;
    endtask
    initial begin
        $readmemh("machine_memory.hex",initial_ram);
        trace_file=$fopen($test$plusargs("mutate_write")?"negative-ordering_trace.csv":"ordering_trace.csv","w");if(!trace_file)$fatal(1,"trace open failed");
        $fdisplay(trace_file,"ntsc,badline,index,sequence,native,accelerated");
        for(integer standard=0;standard<2;standard++)begin
            ntsc=standard;reset_n=0;pause=1;load_all();
            // Index0 with BA policy is deliberately exact. Add index1 there
            // so each standard still checks four genuinely accelerated cases.
            for(integer bad=0;bad<2;bad++)for(integer selection=0;selection<(bad?3:2);selection++)begin
                integer timeout_ticks; reg injected;
                integer index;
                index=selection==0?0:((bad&&selection==1)?1:15);
                comparing=0;reset_n=0;pause=1;badline=bad;rate=index;setup_ready=0;warmed=0;
                repeat(160)@(negedge clk);
                trace_length[0]=0;trace_length[1]=0;trace_started=0;trace_finished=0;
                comparing=1;reset_n=1;pause=0;timeout_ticks=0;injected=0;
                while(trace_finished!=3&&timeout_ticks<1000000)begin
                    @(negedge clk);timeout_ticks++;
                    if(warmed==3)setup_ready=1;
                    if(!injected&&((fall_port0>0&&fall_port1>0)||
                       (exact_policy&&setup_ready&&retired_port0>0&&retired_port1>0)))begin
                        memory_wait=1;repeat(17)@(negedge clk);memory_wait=0;
                        pause=1;repeat(128)@(negedge clk);
                        if(!paused[0]||!paused[1])$fatal(1,"write path pause acknowledgement");
                        repeat(5)@(negedge clk);pause=0;injected=1;
                    end
                end
                if(trace_finished!=3)begin
                    for(integer row=0;row<trace_length[0];row++)
                        $fdisplay(trace_file,"%0d,%0d,%0d,%0d,%07h,%07h",ntsc,badline,index,row,bus_trace[0][row],bus_trace[1][row]);
                    $display("ISLAND state=%0d address=%h wr=%b wp=%0d done=%0d pend=%b/%b/%b/%b ack=%b all=%b grant=%b",lane[1].dut.accelerated.cpu_island_i.state,lane[1].dut.accelerated.cpu_island_i.cpu_address,lane[1].dut.accelerated.cpu_island_i.cpu_write,lane[1].dut.fio_wp,lane[1].dut.fio_done,lane[1].dut.accelerated.cpu_island_i.wr_pend_q,lane[1].dut.accelerated.cpu_island_i.clr_m2m_pend_q,lane[1].dut.accelerated.cpu_island_i.clr_m2d_pend_q,lane[1].dut.accelerated.cpu_island_i.latch_pend_q,lane[1].dut.accelerated.cpu_island_i.ack_wait_q,lane[1].dut.accelerated.cpu_island_i.all_done_q,lane[1].dut.accelerated.cpu_island_i.grant_q);
                    $fatal(1,"ordering timeout ntsc=%0d badline=%0d index=%0d finished=%b lengths=%0d/%0d",ntsc,badline,index,trace_finished,trace_length[0],trace_length[1]);
                end
                if(trace_length[0]!=trace_length[1])$fatal(1,"ordering lengths %0d/%0d",trace_length[0],trace_length[1]);
                for(integer row=0;row<trace_length[0];row++)begin
                    $fdisplay(trace_file,"%0d,%0d,%0d,%0d,%07h,%07h",ntsc,badline,index,row,bus_trace[0][row],bus_trace[1][row]);
                    if(bus_trace[0][row]!==bus_trace[1][row])$fatal(1,"ordering ntsc=%0d badline=%0d index=%0d row=%0d native=%07h turbo=%07h",ntsc,badline,index,row,bus_trace[0][row],bus_trace[1][row]);
                end
                // Directly verify the status acknowledgement while entries
                // drain and the unchanged native pause mechanism stops time.
                pause=1;timeout_ticks=0;
                while(!paused[1]&&timeout_ticks<256)begin @(negedge clk);timeout_ticks++;end
                if(!paused[1])$fatal(1,"ordering pause deadline");
                repeat(5)@(negedge clk);
                total_cycles+=trace_length[0];cases++;
                if(!injected||disabled_writes==0||retired_stack==0||retired_port0==0||retired_port1==0)
                    $fatal(1,"held/retired write coverage missing");
                if(exact_policy&&(fall_port0!=0||fall_port1!=0||fall_regular!=0||fall_colour!=0))
                    $fatal(1,"exact policy issued a fast-memory write");
                if(!exact_policy&&(fall_port0==0||fall_port1==0||fall_regular==0||fall_colour==0))
                    $fatal(1,"write-path nonvacuous coverage p0=%0d p1=%0d regular=%0d colour=%0d disabled=%0d stack=%0d",fall_port0,fall_port1,fall_regular,fall_colour,disabled_writes,retired_stack);
                $display("WRITE_PATH_SUMMARY ntsc=%0d badline=%0d index=%0d exact=%0d cycles=%0d p0=%0d p1=%0d regular=%0d colour=%0d disabled=%0d stack=%0d retired_p0=%0d retired_p1=%0d",ntsc,badline,index,exact_policy,trace_length[0],fall_port0,fall_port1,fall_regular,fall_colour,disabled_writes,retired_stack,retired_port0,retired_port1);
            end
        end
        $fclose(trace_file);
        if(cases!=10)$fatal(1,"accelerated ordering coverage");
        $display("PASS: actual CPU falling-edge write path cases=%0d cycles=%0d fio_writes=%0d",cases,total_cycles,fio_entries_seen);$finish;
    end
    initial begin #100000000;$fatal(1,"ordering global timeout");end
endmodule

// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Actual REU commands cross the production CPU/proxy boundary and hold the
// accelerated instruction engine through native DMA and final release.
module tb_c64_turbo_dma;
    reg clk=0,fastclk=0,ntsc=0,reset_n=0,pause=1;
    initial forever begin
        #(ntsc?7.638:7.9295);fastclk=1;clk=1;
        #(ntsc?7.638:7.9295);fastclk=0;
        #(ntsc?7.638:7.9295);fastclk=1;clk=0;
        #(ntsc?7.638:7.9295);fastclk=0;
    end
    reg [1:0]bios=0;
    reg rom_we=0,loader_we=0;
    reg [13:0]rom_addr=0;
    reg [7:0]rom_data=0,loader_data=0;
    reg [15:0]loader_addr=0;
    reg [7:0]initial_ram[0:65535];
    integer mode=0,fetch=0,deferred=0,cases=0;
    wire [4:0]phase;
    wire [5:0]native_ce;
    wire ba,hold_machine,ram_ce,ram_we;
    wire [15:0]ram_addr;
    for(genvar g=0;g<1;g=g+1)begin:lane
    c64_turbo_exact_lane #(.EXTERNAL(1)) dut(
        .test_config(mode==0 ? 32'd0 : mode==1 ? 32'h45f : 32'h41f),.test_ua_enable(1'b0),
        .clk(clk),.fastclk(fastclk),.reset_n(reset_n),.pause(pause),.memory_wait(1'b0),.ntsc(ntsc),.irq_n(1'b1),.nmi_n(1'b1),
        .bios(bios),.rom_we(rom_we),.rom_addr(rom_addr),.rom_data(rom_data),.loader_we(loader_we),.loader_addr(loader_addr),.loader_data(loader_data),
        .dma_req(1'b0),.dma_we(1'b0),.dma_addr(16'd0),.dma_data(8'd0),.dbg_phase(phase),.dbg_native_ce(native_ce),.dbg_rdy(ba),.dbg_hold(hold_machine),
        .ram_addr(ram_addr),.ram_ce(ram_ce),.ram_we(ram_we));
    end
    wire enabled=lane[0].dut.accelerated.cpu_island_i.cpu_enable;
    wire wr=lane[0].dut.accelerated.cpu_island_i.cpu_write;
    wire [15:0]addr=lane[0].dut.accelerated.cpu_island_i.cpu_address;
    wire [7:0]data=lane[0].dut.accelerated.cpu_island_i.cpu_wdata;
    wire request=lane[0].dut.reu_dma_req;
    wire dma_hold=lane[0].dut.ext_dma_hold;
    reg measuring=0,finished=0,armed=0,started=0,released=0,previous_req=0;
    integer dma_acks=0,commands=0,ff00_native=0,ff00_cpu=0,ff00_local_writes=0,ff00_native_writes=0;
    integer dma_edges=0,hold_clocks=0,wait_clocks=0,ba_low_clocks=0,native_ticks=0,last_native_tick=-1,clocks=0;
    integer completion_markers=0,post_dma_enables=0,exact_trigger_retires=0,rows,traces;
    always @(posedge fastclk)begin
        if(measuring&&enabled)begin
            // Exact mode receives the already accepted native trigger's B0
            // one fast edge after REU request rises. Completing that same
            // transaction is legal; no following CPU cycle may advance.
            if(dma_hold)begin
                if(mode!=0||!wr||addr!==(deferred?16'hff00:16'hdf01)||dma_acks!=0)
                    $fatal(1,"CPU advanced beyond trigger while DMA holds ntsc=%0d mode=%0d state=%0d addr=%h",ntsc,mode,lane[0].dut.accelerated.cpu_island_i.state,addr);
                exact_trigger_retires++;
            end
            if(started&&!dma_hold)begin released=1;post_dma_enables++;end
            if(wr&&addr==16'hff00)ff00_cpu++;
            if(wr&&addr==16'h2106)begin
                if(!released||dma_acks!=64||data!=8'h5a)$fatal(1,"CPU resumed before complete DMA");
                completion_markers++;
            end
            if(wr&&addr==16'h2105&&data==8'ha5)finished=1;
        end
    end
    always @(negedge fastclk)begin
        if(measuring&&lane[0].dut.accelerated.cpu_island_i.cpu_mem_ram_we&&addr==16'hff00)ff00_local_writes++;
    end
    always @(posedge clk)begin
        if(measuring)begin
            clocks++;
            if(native_ce[0])begin
                if(last_native_tick>=0&&clocks-last_native_tick!=32)$fatal(1,"Native SID cadence changed during DMA");
                last_native_tick=clocks;native_ticks++;
            end
            if(hold_machine)$fatal(1,"REU must not stop native machine time");
            if(dma_hold)hold_clocks++;
            if(request&&!lane[0].dut.reu_dma_valid)wait_clocks++;
            if(request&&!ba)ba_low_clocks++;
            if(armed&&request)$fatal(1,"Deferred command started before FF00");
            if(request&&!previous_req)begin
                dma_edges++;started=1;
                if(lane[0].dut.island_turbo!==(mode!=0))$fatal(1,"DMA requested without selected CPU mode");
                if(mode!=0&&lane[0].dut.island_active_index!=15)$fatal(1,"DMA test was not64x");
            end
            previous_req=request;
            if(lane[0].dut.ext_cpu_bus_valid&&lane[0].dut.ext_cpu_we)begin
                if(lane[0].dut.ext_cpu_addr==16'hdf01)begin commands++;if(deferred)armed=1;end
                if(lane[0].dut.ext_cpu_addr==16'hff00)begin
                    if(!armed)$fatal(1,"Unarmed FF00 trigger");armed=0;ff00_native++;
                end
            end
            if(ram_ce&&ram_we&&ram_addr==16'hff00)ff00_native_writes++;
            if(lane[0].dut.reu_dma_ack)begin
                if(dma_acks>=64||lane[0].dut.reu_dma_idle||lane[0].dut.reu_dma_peek)$fatal(1,"Unexpected DMA cycle");
                if(lane[0].dut.reu_dma_addr!==(fetch?16'h3000:16'h3100)+dma_acks || lane[0].dut.reu_dma_we!==fetch[0])$fatal(1,"Wrong DMA address/direction");
                if((fetch?lane[0].dut.reu_dma_data:lane[0].dut.reu_dma_result)!==(fetch?8'h80:8'h40)+dma_acks)$fatal(1,"Wrong DMA byte index=%0d",dma_acks);
                $fdisplay(traces,"%0d,%0d,%0d,%0d,%0d,%04h,%02h,%0d",ntsc,mode,fetch,deferred,dma_acks,lane[0].dut.reu_dma_addr,fetch?lane[0].dut.reu_dma_data:lane[0].dut.reu_dma_result,lane[0].dut.reu_dma_we);
                dma_acks++;
            end
        end
    end
    task automatic load_all;
        @(negedge clk);loader_we=1;
        for(integer a=0;a<65536;a++)begin loader_addr=a;loader_data=initial_ram[a];@(negedge clk);end
        loader_we=0;bios=0;rom_we=1;
        for(integer a=0;a<16384;a++)begin rom_addr=a;rom_data=a==14'h3ffc ? 0 : a==14'h3ffd ? 7 : 8'hea;@(negedge clk);end
        bios=1;for(integer a=0;a<4096;a++)begin rom_addr=a;rom_data=8'hc7;@(negedge clk);end
        rom_we=0;bios=0;
    endtask
    initial begin
        integer timeout_ticks;string file_name;
        rows=$fopen("dma_summary.csv","w");traces=$fopen("dma_trace.csv","w");
        $fdisplay(rows,"ntsc,mode,fetch,deferred,bytes,commands,ff00_native,ff00_cpu,ff00_local_writes,ff00_native_writes,hold_clocks,wait_clocks,ba_low_clocks,native_ticks,post_dma_enables,exact_trigger_retires");
        $fdisplay(traces,"ntsc,mode,fetch,deferred,byte,addr,data,write");
        for(integer video=0;video<2;video++)for(integer speed=0;speed<3;speed++)for(integer direction=0;direction<2;direction++)for(integer trigger=0;trigger<2;trigger++)begin
            measuring=0;reset_n=0;pause=1;ntsc=video;mode=speed;fetch=direction;deferred=trigger;
            file_name=$sformatf("dma-%0d-%0d.hex",fetch,deferred);$readmemh(file_name,initial_ram);load_all();
            for(integer a=0;a<512;a++)lane[0].dut.reu_memory[a]=a<64 ? 8'h80+a : 8'hcc;
            repeat(160)@(negedge clk);
            finished=0;armed=0;started=0;released=0;previous_req=0;
            dma_acks=0;commands=0;ff00_native=0;ff00_cpu=0;ff00_local_writes=0;ff00_native_writes=0;dma_edges=0;
            hold_clocks=0;wait_clocks=0;ba_low_clocks=0;native_ticks=0;last_native_tick=-1;clocks=0;completion_markers=0;post_dma_enables=0;exact_trigger_retires=0;
            measuring=1;reset_n=1;pause=0;timeout_ticks=0;
            while(!finished&&timeout_ticks<1500000)begin @(negedge clk);timeout_ticks++;end
            if(!finished)$fatal(1,"DMA integration timeout ntsc=%0d mode=%0d fetch=%0d deferred=%0d addr=%h acks=%0d",ntsc,mode,fetch,deferred,addr,dma_acks);
            repeat(32)@(negedge clk);measuring=0;
            if(dma_acks!=64||commands!=1||dma_edges!=1||ff00_native!=deferred||ff00_cpu!=deferred||completion_markers!=1||hold_clocks==0||wait_clocks==0||ba_low_clocks==0||native_ticks<64||!released)
                $fatal(1,"Incomplete real DMA coverage ntsc=%0d mode=%0d fetch=%0d deferred=%0d acks=%0d commands=%0d edges=%0d ff00=%0d/%0d completion=%0d hold=%0d waits=%0d BA=%0d",ntsc,mode,fetch,deferred,dma_acks,commands,dma_edges,ff00_native,ff00_cpu,completion_markers,hold_clocks,wait_clocks,ba_low_clocks);
            for(integer a=0;a<64;a++)begin
                if(lane[0].dut.accelerated.main_ram_i.main_low[16'h2200+a]!==((fetch?8'h80:8'h40)+a))$fatal(1,"CPU readback after DMA byte%0d",a);
                if(fetch)begin
                    if(lane[0].dut.accelerated.main_ram_i.main_low[16'h3000+a]!==8'h80+a)$fatal(1,"Fetch final RAM byte%0d",a);
                end else if(lane[0].dut.reu_memory[256+a]!==8'h40+a)$fatal(1,"Stash final REU byte%0d",a);
            end
            if(lane[0].dut.accelerated.main_ram_i.main_low[16'h2107][6]!==1'b1)$fatal(1,"Missing REU EOB status");
            if(lane[0].dut.write_bytes!==(fetch?0:64))$fatal(1,"Wrong REU backing-write count");
            if(exact_trigger_retires!=(mode==0))$fatal(1,"Exact accepted trigger completion count");
            $fdisplay(rows,"%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d",ntsc,mode,fetch,deferred,dma_acks,commands,ff00_native,ff00_cpu,ff00_local_writes,ff00_native_writes,hold_clocks,wait_clocks,ba_low_clocks,native_ticks,post_dma_enables,exact_trigger_retires);
            $display("DMA_SUMMARY ntsc=%0d mode=%0d fetch=%0d deferred=%0d bytes=%0d FF00RAM=%0d/%0d",ntsc,mode,fetch,deferred,dma_acks,ff00_local_writes,ff00_native_writes);cases++;
        end
        $fclose(rows);$fclose(traces);$display("TURBO_DMA PASS cases=%0d",cases);$finish;
    end
    initial begin #2000000000;$fatal(1,"DMA integration global timeout");end
endmodule

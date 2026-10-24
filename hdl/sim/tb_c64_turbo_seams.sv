// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Force three production policy causes while FIO is pending, then release
// before a chosen exact transaction. Compare every accepted CPU bus cycle.
module tb_c64_turbo_seams;
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
    integer cause=0,target=0,cases=0;
    reg force_cause=0;
    wire [4:0]phase;
    wire fast_vic_wr,fast_sid_wr;
    wire [5:0]fast_vic_wa;
    wire [9:0]fast_sid_a;
    for(genvar g=0;g<1;g=g+1)begin: lane
    c64_turbo_exact_lane #(.EXTERNAL(1)) dut(
        .test_config(cause==0 ? 32'd0 : 32'h51f),
        .test_guard(force_cause&&cause==1),.test_loader(force_cause&&cause==2),.test_cart(force_cause&&cause==3),
        .seam_vic_wr(fast_vic_wr),.seam_vic_wa(fast_vic_wa),.seam_sid_wr(fast_sid_wr),.seam_sid_a(fast_sid_a),
        .clk(clk),.fastclk(fastclk),.reset_n(reset_n),.pause(pause),.memory_wait(1'b0),.ntsc(ntsc),.irq_n(1'b1),.nmi_n(1'b1),
        .bios(bios),.rom_we(rom_we),.rom_addr(rom_addr),.rom_data(rom_data),.loader_we(loader_we),.loader_addr(loader_addr),.loader_data(loader_data),
        .dma_req(1'b0),.dma_we(1'b0),.dma_addr(16'd0),.dma_data(8'd0),.dbg_phase(phase));
    end
    wire enabled=lane[0].dut.accelerated.cpu_island_i.cpu_enable;
    wire wr=lane[0].dut.accelerated.cpu_island_i.cpu_write;
    wire [15:0]addr=lane[0].dut.accelerated.cpu_island_i.cpu_address;
    wire [7:0]data=wr ? lane[0].dut.accelerated.cpu_island_i.cpu_wdata : lane[0].dut.accelerated.cpu_island_i.cpu_di;
    wire turbo=lane[0].dut.island_turbo;
    reg [15:0]target_addr;
    reg target_write;
    reg measuring=0,started=0,finished=0,tripped=0,released=0,saw_exact=0,saw_return=0;
    reg [24:0]reference[0:4095];
    integer length=0,reference_length=0,trip_pending=0,target_retires=0,target_native=0;
    integer vic_writes=0,sid_writes=0,ack_writes=0,collision_reads=0,cia_reads=0;
    integer rows,trace_rows;
    always @(negedge fastclk)begin
        if(measuring&&cause!=0)begin
            if(started&&!tripped&&lane[0].dut.fio_wp!=lane[0].dut.fio_done)begin
                if(!turbo)$fatal(1,"Trip must start accelerated");
                force_cause=1;tripped=1;trip_pending++;
            end
            if(tripped&&!turbo)saw_exact=1;
            if(saw_exact&&!released&&addr==target_addr&&wr==target_write&&phase==0)begin
                force_cause=0;released=1;
            end
            if(released&&target_retires==1&&turbo)saw_return=1;
        end
    end
    always @(posedge fastclk)begin
        if(measuring&&enabled&&!finished)begin
            if(!wr&&addr==16'h0800)started=1;
            if(started)begin
                if(length>=4096)$fatal(1,"Seam trace bound");
                if(cause==0)reference[length]={addr,wr,data};
                else begin
                    $fdisplay(trace_rows,"%0d,%0d,%0d,%0d,%07h,%07h",ntsc,target,cause,length,reference[length],{addr,wr,data});
                    if(reference[length]!=={addr,wr,data})$fatal(1,"Seam bus divergence ntsc=%0d target=%0d cause=%0d row=%0d expected=%h got=%h",ntsc,target,cause,length,reference[length],{addr,wr,data});
                end
                length++;
                if(addr==target_addr&&wr==target_write)begin
                    target_retires++;
                    if(cause!=0&&(!released||turbo))$fatal(1,"Target must complete exact after cause release");
                end
                if(wr&&addr==16'h2105&&data==8'ha5)finished=1;
            end
        end
    end
    always @(posedge clk)begin
        if(measuring&&started)begin
            if(lane[0].dut.ext_cpu_bus_valid)begin
                if(lane[0].dut.ext_cpu_addr==target_addr&&lane[0].dut.ext_cpu_we==target_write)target_native++;
                if(lane[0].dut.ext_cpu_we&&lane[0].dut.ext_cpu_addr==16'hd020)vic_writes++;
                if(lane[0].dut.ext_cpu_we&&lane[0].dut.ext_cpu_addr==16'hd418)sid_writes++;
                if(lane[0].dut.ext_cpu_we&&lane[0].dut.ext_cpu_addr==16'hd019)ack_writes++;
                if(!lane[0].dut.ext_cpu_we&&lane[0].dut.ext_cpu_addr==16'hd01e)collision_reads++;
                if(!lane[0].dut.ext_cpu_we&&lane[0].dut.ext_cpu_addr==16'hdc0d)cia_reads++;
            end
            if(fast_vic_wr&&fast_vic_wa==6'h20)vic_writes++;
            if(fast_sid_wr&&fast_sid_a==10'h018)sid_writes++;
            if(fast_vic_wr&&fast_vic_wa==6'h19)ack_writes++;
        end
    end
    task automatic load_all;
        @(negedge clk);loader_we=1;
        for(integer a=0;a<65536;a++)begin loader_addr=a;loader_data=initial_ram[a];@(negedge clk);end
        loader_we=0;bios=0;rom_we=1;
        for(integer a=0;a<16384;a++)begin rom_addr=a;rom_data=a==14'h3ffc ? 0 : a==14'h3ffd ? 7 : 8'hea;@(negedge clk);end
        bios=1;
        for(integer a=0;a<4096;a++)begin rom_addr=a;rom_data=8'hc7;@(negedge clk);end
        rom_we=0;bios=0;
    endtask
    initial begin
        integer timeout_ticks;
        string file_name;
        rows=$fopen("seams.csv","w");trace_rows=$fopen("seam_trace.csv","w");
        $fdisplay(rows,"ntsc,target,cause,cycles,pending_at_trip,target_retires,target_native,vic_writes,sid_writes,ack_writes,collision_reads,cia_reads");
        $fdisplay(trace_rows,"ntsc,target,cause,sequence,reference,observed");
        for(integer video=0;video<2;video++)for(integer item=0;item<5;item++)begin
            measuring=0;reset_n=0;pause=1;force_cause=0;ntsc=video;target=item;
            case(item)
                0:begin target_addr=16'h2200;target_write=1;end
                1:begin target_addr=16'hdc0d;target_write=0;end
                2:begin target_addr=16'hd418;target_write=1;end
                3:begin target_addr=16'hd019;target_write=1;end
                4:begin target_addr=16'hd01e;target_write=0;end
            endcase
            file_name=$sformatf("seam-%0d.hex",target);$readmemh(file_name,initial_ram);load_all();
            for(integer test=0;test<4;test++)begin
                measuring=0;reset_n=0;pause=1;cause=test;force_cause=0;
                repeat(160)@(negedge clk);
                started=0;finished=0;tripped=0;released=0;saw_exact=0;saw_return=0;
                length=0;trip_pending=0;target_retires=0;target_native=0;
                vic_writes=0;sid_writes=0;ack_writes=0;collision_reads=0;cia_reads=0;
                measuring=1;reset_n=1;pause=0;timeout_ticks=0;
                while(!finished&&timeout_ticks<200000)begin @(negedge clk);timeout_ticks++;end
                if(!finished)$fatal(1,"Seam timeout target=%0d cause=%0d addr=%h exact=%b release=%b",target,cause,addr,saw_exact,released);
                repeat(32)@(negedge clk);measuring=0;
                if(cause==0)reference_length=length;
                else if(length!=reference_length||!tripped||!released||!saw_exact||!saw_return||trip_pending!=1)
                    $fatal(1,"Seam coverage target=%0d cause=%0d lengths=%0d/%0d trip=%b release=%b exact=%b return=%b pending=%0d",target,cause,length,reference_length,tripped,released,saw_exact,saw_return,trip_pending);
                if(target_retires!=1||target_native!=1||vic_writes!=3||sid_writes!=(target==2)||ack_writes!=(target==3)||collision_reads!=(target==4)||cia_reads!=(target==1))
                    $fatal(1,"Duplicate/missing seam side effect target=%0d cause=%0d retires=%0d native=%0d vic=%0d sid=%0d ack=%0d collision=%0d cia=%0d",target,cause,target_retires,target_native,vic_writes,sid_writes,ack_writes,collision_reads,cia_reads);
                if(target==0&&lane[0].dut.accelerated.main_ram_i.main_low[16'h2200]!==8'h5a)$fatal(1,"Exact last RAM write lost");
                $fdisplay(rows,"%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d",ntsc,target,cause,length,trip_pending,target_retires,target_native,vic_writes,sid_writes,ack_writes,collision_reads,cia_reads);
                $display("SEAM_SUMMARY ntsc=%0d target=%0d cause=%0d cycles=%0d",ntsc,target,cause,length);cases++;
            end
        end
        $fclose(rows);$fclose(trace_rows);$display("TURBO_SEAMS PASS cases=%0d",cases);$finish;
    end
    initial begin #1000000000;$fatal(1,"Seam global timeout");end
endmodule

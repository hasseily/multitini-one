// SPDX-License-Identifier: GPL-3.0-only
// Production T65, island, proxy and synchronous memory driven by unmodified
// VICE CPU-attempt schedules. Native peripheral bus is a flat RAM/ROM model.
`timescale 1ns/1ps
module tb_c64_island_vice;
    integer video=0;
    reg clk_cpu=0,clk_native=0,ext_reset=1;
    always begin #(video==0?7.9295:7.638) clk_cpu=~clk_cpu;end
    always @(posedge clk_cpu) clk_native<=~clk_native;
    reg [4:0] ext_phase=0;
    reg ext_frame_run=1,ext_rdy=1,ext_irq_n=1,ext_nmi_n=1;wire ext_dma_hold=0;
    always @(posedge clk_native)if(ext_frame_run)ext_phase<=ext_phase+1'b1;
    wire ext_cpu_enable=(ext_phase==31)&&!ext_reset&&ext_frame_run;
    wire ext_cpu_bus_valid=ext_cpu_enable;
    wire ext_port_tick=(ext_phase==0)&&!ext_reset&&ext_frame_run;
    wire [7:0] ext_cpu_di;
    reg policy_turbo=0;
    reg [3:0] policy_index=15;
    wire [15:0] ext_cpu_addr,isl_x_req_addr;
    wire [7:0] ext_cpu_do,ext_cpu_io,isl_x_req_wdata,isl_x_req_port;
    wire ext_cpu_we,ext_cpu_ioonly,isl_x_req_we,isl_x_req_exact,isl_x_req_ioonly,isl_x_req_seq,isl_x_req_valid;
    wire [7:0] b0_data;
    wire b0_irq_n,b0_nmi_n,b0_rdy,b0_toggle,b0_exact,b0_seq;
    wire b1_toggle,b1_dma_hold,b1_turbo,b1_badline,b1_pause,b1_c64u_top;
    wire [3:0] b1_index;
    wire port_tick_toggle,irq_safe_toggle,frame_valid,frame_exact,done_seq,diag_deadline_miss;
    multitini_c64_cpu_proxy proxy_i (
        .clk_native(clk_native),.reset(ext_reset),.ext_phase(ext_phase),.ext_frame_run(ext_frame_run),
        .ext_cpu_enable(ext_cpu_enable),.ext_cpu_bus_valid(ext_cpu_bus_valid),.ext_cpu_di(ext_cpu_di),
        .ext_irq_n(ext_irq_n),.ext_nmi_n(ext_nmi_n),.ext_rdy(ext_rdy),.ext_port_tick(ext_port_tick),
        .ext_dma_hold(ext_dma_hold),.policy_turbo(policy_turbo),.policy_blocked(1'b0),
        .policy_badline(1'b0),.policy_c64u_top(1'b0),.policy_index(policy_index),
        .pause_request(1'b0),.cart_present(1'b0),.*
    );
    wire cpu_mem_ce,cpu_mem_ram_we,cpu_mem_colour_we;
    wire [15:0] cpu_mem_addr;
    wire [7:0] cpu_mem_wdata,ram_low_q,ram_high_q,rom_q,char_q;
    wire [3:0] colour_q;
    wire [15:0] cpu_address;
    wire [7:0] cpu_wdata,cpu_port,test_data;
    wire test_sync;
    wire cpu_write,cpu_enable,nmi_ack,turbo_active,active_badline,active_c64u_top,diag_nmi_used;
    wire [3:0] active_index;
    wire [5:0] fast_slot;
    wire [9:0] fio_rd_addr;
    wire fio_rd_sid;
    wire [87:0] fio_entries;
    wire [2:0] fio_wp;
    multitini_c64_cpu_island island_i (
        .c64_aresetn(1'b1),.diag_armed(1'b0),.pause_request(1'b0),.ext_last_vic_di(8'hcc),.external_port_input(8'h17),
        .fio_rd_data(8'hff),.fio_vic_busy(1'b0),.fio_done(3'b0),.*
    );
    wire [7:0] machine_q;
    wire machine_ce=(ext_phase==28)&&!ext_reset&&ext_frame_run;
    multitini_c64_island_memory memory_i (
        .clk_cpu(clk_cpu),.clk_native(clk_native),.cpu_ce(cpu_mem_ce),
        .cpu_ram_we(cpu_mem_ram_we),.cpu_colour_we(cpu_mem_colour_we),
        .cpu_addr(cpu_mem_addr),.cpu_wdata(cpu_mem_wdata),
        .ram_low_q(ram_low_q),.ram_high_q(ram_high_q),.rom_q(rom_q),.char_q(char_q),.colour_q(colour_q),
        .loader_we(1'b0),.loader_addr(16'b0),.loader_data(8'b0),
        .machine_ce(machine_ce),.machine_we(ext_cpu_we),.machine_store(1'b1),
        .machine_addr(ext_cpu_addr),.machine_data((ext_cpu_addr<2)?8'hcc:ext_cpu_do),.machine_q(machine_q),
        .peek_request(1'b0),.machine_paused(1'b0),.peek_addr(16'b0),.peek_valid(),.peek_data(),
        .native_colour_addr(10'b0),.native_colour_we(1'b0),.native_colour_data(4'b0),.native_colour_q(),
        .rom_load_addr(14'b0),.rom_load_data(8'b0),.rom_load_basic_kernal(1'b0),.rom_load_char(1'b0)
    );
    assign ext_cpu_di=machine_q;
    reg [2:0] controls[0:2047];
    integer scenario,access_count,tick,clocks,holds,writes_while_held,cases_file,count,parsed,cadence;
    integer paused_edges=0,source_file;
    string memory_file,controls_file;
    reg tracing=0,finished=0;
    always @(negedge clk_cpu) begin
        if(tracing && !finished)begin
            ext_rdy=controls[tick][2];ext_irq_n=!controls[tick][1];ext_nmi_n=!controls[tick][0];
        end else begin ext_rdy=1;ext_irq_n=1;ext_nmi_n=1;end
    end
    // The alternate spacing inserts a native machine wait at phase 12.
    // The complete phase, proxy and memory stop together; no CPU tick is added.
    always @(negedge clk_native)begin
        if(ext_reset)begin ext_frame_run=1;paused_edges=0;end
        else if(cadence==2 && ext_phase==12 && paused_edges<2)begin
            ext_frame_run=0;paused_edges++;
        end else begin
            ext_frame_run=1;
            if(ext_phase!=12)paused_edges=0;
        end
    end
    always @(posedge clk_cpu) begin
        if(!ext_reset && cpu_enable)begin
            if(turbo_active)$fatal(1,"VICE exact matrix unexpectedly entered turbo");
            if(!tracing && test_sync && !cpu_write && cpu_address==16'h0800)begin tracing=1;tick=0;end
            if(tracing && !finished)begin
                if({b0_rdy,!b0_irq_n,!island_i.t65_nmi_n}!==controls[tick])
                    $fatal(1,"Pin schedule drift case=%0d tick=%0d actual=%b expected=%b",scenario,tick,
                        {b0_rdy,!b0_irq_n,!island_i.t65_nmi_n},controls[tick]);
                if(b0_rdy || cpu_write)begin
                    if($isunknown({cpu_address,cpu_write,test_data,test_sync}))$fatal(1,"Unknown CPU bus");
                    $display("BOUNDARY_BUS,%0d,%0d,%0d,%04h,%s,%02h,%0d",scenario,access_count,tick,
                        cpu_address,cpu_write?"W":"R",cpu_write?cpu_wdata:test_data,test_sync);
                    access_count++;
                    if(cpu_write && !b0_rdy)writes_while_held++;
                    if(cpu_write && cpu_address==16'h0204 && cpu_wdata==8'ha5)finished=1;
                end else holds++;
                tick++;
            end
        end
    end
    task automatic run_case;
        @(negedge clk_cpu);ext_reset=1;tracing=0;finished=0;access_count=0;tick=0;
        holds=0;writes_while_held=0;clocks=0;
        $readmemh("low.hex",memory_i.main_low);$readmemh("high.hex",memory_i.main_high);
        $readmemh("rom.hex",memory_i.basic_kernal);$readmemh("char.hex",memory_i.character);
        $readmemh(controls_file,controls);
        repeat(4)begin wait(ext_phase==31);@(posedge clk_native);#0.01;end
        @(negedge clk_cpu);ext_reset=0;
        while(!finished && clocks<160000 && tick<2047)begin @(negedge clk_cpu);clocks++;end
        $display("BOUNDARY_SUMMARY,%0d,%0d,%0d,%0d,%0d,%0d",scenario,finished,access_count,tick,holds,writes_while_held);
        if(!finished)$fatal(1,"Island VICE matrix timeout case=%0d addr=%x state=%0d",scenario,cpu_address,island_i.state);
    endtask
    initial begin
        source_file=$fopen("video.txt","r");parsed=$fscanf(source_file,"%d",video);$fclose(source_file);
        if(parsed!=1)$fatal(1,"Missing video fixture");
        cases_file=$fopen("cases.txt","r");parsed=$fscanf(cases_file,"%d",count);
        if(parsed!=1)$fatal(1,"Missing case count");
        for(integer index=0;index<count;index++)begin
            parsed=$fscanf(cases_file,"%d %d %s %s",scenario,cadence,memory_file,controls_file);
            if(parsed!=4)$fatal(1,"Missing case row");run_case();
        end
        $fclose(cases_file);$display("ISLAND_VICE PASS video=%0d cases=%0d",video,count);$finish;
    end
    initial begin #2000000000;$fatal(1,"Island VICE harness timeout");end
endmodule

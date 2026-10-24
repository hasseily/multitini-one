// SPDX-License-Identifier: GPL-3.0-only
// Real production T65 on both sides; draft island/proxy/BRAM on DUT side.
// Native bus model here is RAM/ROM only. Whole-machine lockstep is a later gate.
`timescale 1ns/1ps
module tb_island_cpu;
    reg clk_cpu=0,clk_native=0,ext_reset=1;
    always #5 clk_cpu=~clk_cpu;
    always @(posedge clk_cpu) clk_native<=~clk_native;
    reg [4:0] ext_phase=0;
    always @(posedge clk_native)ext_phase<=ext_phase+1'b1;
    wire ext_frame_run=1,ext_rdy=1,ext_irq_n=1,ext_nmi_n=1,ext_dma_hold=0;
    wire ext_cpu_enable=(ext_phase==31)&&!ext_reset;
    wire ext_cpu_bus_valid=ext_cpu_enable;
    wire ext_port_tick=(ext_phase==0)&&!ext_reset;
    wire [7:0] ext_cpu_di;
    reg policy_turbo=0;
    reg [3:0] policy_index=15;
    integer mode=16;
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
    wire machine_ce=(ext_phase==28)&&!ext_reset;
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
    reg [7:0] reference_ram[0:65535];
    reg [1:0] ref_div=0;
    always @(posedge clk_cpu)ref_div<=ref_div+1'b1;
    wire ref_enable=ref_div==0 && !ext_reset;
    reg ref_tick_seen=0;
    always @(posedge clk_cpu)ref_tick_seen<=port_tick_toggle;
    wire [15:0] ref_addr;
    wire [7:0] ref_do,ref_port,ref_effective;
    wire ref_we,ref_nmi_ack;
    cpu_6510 reference_i (
        .clk(clk_cpu),.enable(ref_enable),.reset(ext_reset),.bus_start(1'b0),
        .nmi_n(1'b1),.nmi_ack(ref_nmi_ack),.irq_n(1'b1),.rdy(1'b1),
        .port_tick(port_tick_toggle!=ref_tick_seen),.di(reference_ram[ref_addr]),.\do (ref_do),
        .addr(ref_addr),.we(ref_we),.diIO(8'h17),.doIO(ref_port),.test_data(ref_effective)
    );
    integer rf,df,mf,scan_result,rcount=0,dcount=0;
    reg rtrace=0,dtrace=0,rdone=0,ddone=0;
    reg [63:0] enable_window=0;
    integer turbo_enables=0,turbo_grants=0,max_window=0,window_count=0,rate=0;
    always @(posedge clk_cpu) begin
        if(turbo_active)begin
            if(cpu_enable)turbo_enables++;
            if(island_i.grant_q)turbo_grants++;
            if(turbo_enables>turbo_grants)$fatal(1,"CPU spent a grant twice");
            enable_window={enable_window[62:0],cpu_enable};
            window_count=$countones(enable_window);
            if(window_count>max_window)max_window=window_count;
            if(window_count>rate+1)$fatal(1,"CPU exceeds N+1 window contract");
        end
        if(ref_enable && !rdone)begin
            if(!ref_we && ref_addr==16'h0800)rtrace=1;
            if(rtrace)begin
                $fdisplay(rf,"%04x,%d,%02x",ref_addr,ref_we,ref_effective);rcount++;
                if(ref_we && ref_addr==16'h0204 && ref_do==8'ha5)rdone=1;
            end
            if(ref_we)reference_ram[ref_addr]<=(ref_addr<2)?8'hcc:ref_do;
        end
        if(cpu_enable && !ddone)begin
            if(!cpu_write && cpu_address==16'h0800)dtrace=1;
            if(dtrace)begin
                $fdisplay(df,"%04x,%d,%02x",cpu_address,cpu_write,test_data);dcount++;
                if(mode!=16)policy_turbo<=1;
                if(cpu_write && cpu_address==16'h0204 && cpu_wdata==8'ha5)ddone=1;
            end
        end
        if(rdone && ddone)begin
            if(mode!=16 && turbo_enables<100)$fatal(1,"Turbo case did not exercise turbo");
            if(mode==16 && turbo_enables!=0)$fatal(1,"Exact mode used turbo");
            if(mode==15 && max_window<60)$fatal(1,"64x case never sustained dense RAM cycles");
            $fclose(rf);$fclose(df);
            $display("ISLAND_CPU PASS reference=%0d island=%0d mode=%0d turbo_enables=%0d grants=%0d max_window=%0d",rcount,dcount,mode,turbo_enables,turbo_grants,max_window);$finish;
        end
    end
    initial begin
        mf=$fopen("mode.txt","r");scan_result=$fscanf(mf,"%d",mode);$fclose(mf);
        if(scan_result!=1)$fatal(1,"missing mode fixture");
        policy_index=mode;
        case(mode)
            0:rate=1;1:rate=2;2:rate=3;3:rate=4;4:rate=6;5:rate=8;6:rate=10;7:rate=12;
            8:rate=14;9:rate=16;10:rate=20;11:rate=24;12:rate=32;13:rate=40;14:rate=48;default:rate=64;
        endcase
        $readmemh("memory.hex",reference_ram);
        $readmemh("low.hex",memory_i.main_low);$readmemh("high.hex",memory_i.main_high);
        $readmemh("rom.hex",memory_i.basic_kernal);$readmemh("char.hex",memory_i.character);
        rf=$fopen("reference.csv","w");df=$fopen("island.csv","w");
        repeat(4)begin wait(ext_phase==31);@(posedge clk_native);#0.01;end
        ext_reset=0;
    end
    initial begin #3000000;$fatal(1,"CPU timeout ref=%d dut=%d addr=%x state=%d",rcount,dcount,cpu_address,island_i.state);end
endmodule

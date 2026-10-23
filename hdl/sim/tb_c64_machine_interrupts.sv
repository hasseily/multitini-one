// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Actual production machine and memory. Only extra observation ports are
// generated; all video steals come from the real VIC and guest register writes.
module tb_c64_machine_interrupts;
    logic clk=0,reset_n=0,pause=1,ntsc=0;
    logic [1:0] turbo_mode=0,turbo_speed=0,bios=0;
    logic rom_we=0,loader_we=0,irq_n=1,nmi_n=1;
    logic [13:0] rom_addr=0;
    logic [7:0] rom_data=0,loader_data=0;
    logic [15:0] loader_addr=0;
    wire [15:0] ram_addr,dbg_cpu_addr;
    wire [7:0] ram_data,ram_q,dbg_cpu_di,dbg_cpu_do,dbg_cpu_port,dbg_vic_bus;
    wire ram_ce,ram_we,paused,dbg_cpu_we,dbg_request,dbg_retire,dbg_reset,dbg_hold;
    wire dbg_rdy,dbg_cpu_owned,dbg_dma,dbg_io_write,test_cpu_sync;
    wire [4:0] dbg_phase,dbg_io_cs;
    wire [5:0] dbg_native_ce;
    wire [9:0] test_vic_x;
    wire [8:0] test_vic_y;
    logic [7:0] initial_ram[0:65535];
    logic running=0,finished=0,irq_sent=0,nmi_sent=0,irq_resume=0,nmi_resume=0;
    integer dense=0,clocks=0,irq_entries=0,nmi_entries=0,irq_rti=0,nmi_rti=0;
    integer held_reads=0,badline_holds=0,sprite_holds=0,ba_writes=0,extra_cycles=0;
    integer irq_inject_y=-1,nmi_inject_y=-1,ram_writes=0;
    logic held_check=0,write_check=0;
    logic [15:0] held_address,saved_write_address;
    logic [7:0] saved_write_data;
    logic late_mode=0,late_after_issue=0,late_second_sent=0,first_fetch_retired=0,first_body_issued=0;
    logic pending=0,pending_we;
    logic [15:0] pending_address,first_body_address;
    logic [7:0] pending_write_data;
    integer pending_checks=0,nmi_vectors=0,nmi_vector_highs=0,stack_count=0,second_stack_count=0;
    logic [15:0] stack_address[0:31];
    logic [7:0] stack_data[0:31];
    always #5 clk=~clk;
    multitini_c64_local_ram memory_i(.clk(clk),.loader_we(loader_we),.loader_addr(loader_addr),
        .loader_data(loader_data),.machine_ce(ram_ce),.machine_we(ram_we),.machine_store(1'b1),
        .machine_addr(ram_addr),.machine_data(ram_data),.machine_q(ram_q));
    fpga64_sid_iec dut(
        .clk32(clk),.reset_n(reset_n),.bios(bios),.pause(pause),.memory_wait(1'b0),.pause_out(paused),
        .ps2_key(11'd0),.kbd_reset(!reset_n),.shift_mod(2'b11),
        .ramAddr(ram_addr),.ramDin(ram_q),.ramDout(ram_data),.ramCE(ram_ce),.ramWE(ram_we),
        .io_cycle(),.ext_cycle(),.refresh(),.cia_mode(1'b0),.turbo_mode(turbo_mode),.turbo_speed(turbo_speed),
        .local_turbo_allowed(1'b1),.vic_variant(2'b00),.ntscMode(ntsc),.hsync(),.vsync(),
        .palette(3'b000),.r(),.g(),.b(),.game(1'b1),.exrom(1'b1),.io_rom(1'b0),.io_ext(1'b0),.io_data(8'hff),
        .irq_n(irq_n),.nmi_n(nmi_n),.nmi_ack(),.romL(),.romH(),.UMAXromH(),.IOE(),.IOF(),
        .freeze_key(),.mod_key(),.tape_play(),.tape_rew(),.tape_stop(),.tape_ff(),.tape_reset_counter(),
        .dma_req(1'b0),.dma_cycle(),.dma_addr(16'd0),.dma_dout(8'd0),.dma_din(),.dma_we(1'b0),.irq_ext_n(1'b1),
        .joyA(7'h7f),.joyB(7'h7f),.pot1(8'hff),.pot2(8'hff),.pot3(8'hff),.pot4(8'hff),.audio_l(),.audio_r(),
        .sid_filter(2'b11),.sid_ver(2'b00),.sid_mode(3'b000),.sid_cfg(4'b0000),
        .sid_fc_off_l(13'd0),.sid_fc_off_r(13'd0),.sid_ld_clk(clk),.sid_ld_addr(12'd0),
        .sid_ld_data(16'd0),.sid_ld_wr(1'b0),.sid_digifix(1'b0),
        .pb_i(8'hff),.pb_o(),.pb_oe_o(),.pa2_i(1'b1),.pa2_o(),.pc2_n_o(),.flag2_n_i(1'b1),
        .sp2_i(1'b1),.sp2_o(),.sp1_i(1'b1),.sp1_o(),.cnt2_i(1'b1),.cnt2_o(),.cnt1_i(1'b1),.cnt1_o(),
        .iec_data_o(),.iec_data_i(1'b1),.iec_clk_o(),.iec_clk_i(1'b1),.iec_atn_o(),
        .c64rom_addr(rom_addr),.c64rom_data(rom_data),.c64rom_wr(rom_we),
        .cass_motor(),.cass_write(),.cass_sense(1'b1),.cass_read(1'b1),
        .dbg_phase(dbg_phase),.dbg_cpu_addr(dbg_cpu_addr),.dbg_cpu_di(dbg_cpu_di),.dbg_cpu_do(dbg_cpu_do),
        .dbg_cpu_we(dbg_cpu_we),.dbg_request(dbg_request),.dbg_cpu_port(dbg_cpu_port),.dbg_vic_bus(dbg_vic_bus),
        .dbg_retire(dbg_retire),.dbg_reset(dbg_reset),.dbg_hold(dbg_hold),.dbg_rdy(dbg_rdy),
        .dbg_cpu_owned(dbg_cpu_owned),.dbg_dma(dbg_dma),.dbg_native_ce(dbg_native_ce),.dbg_io_cs(dbg_io_cs),
        .dbg_io_write(dbg_io_write),.test_vic_x(test_vic_x),.test_vic_y(test_vic_y),.test_cpu_sync(test_cpu_sync));

    task automatic check(input logic okay,input string message);
        if(okay!==1'b1)$fatal(1,"FAIL: interrupt integration ntsc=%0d dense=%0d raster=%0d %s",ntsc,dense,test_vic_y,message);
    endtask
    always @(posedge clk) begin
        held_check=0;write_check=0;
        if(running&&!dbg_reset)begin
            if(pending)begin
                check(dbg_cpu_addr===pending_address&&dbg_cpu_we===pending_we,
                    "CPU address/direction changed after scheduler issue");
                if(pending_we)check(dbg_cpu_do===pending_write_data,"CPU write data changed in flight");
                pending_checks++;
                if(dbg_retire)begin
                    if(late_mode&&!pending_we&&pending_address>=16'h0900&&pending_address<16'h0c00)
                        check(dbg_cpu_di===memory_i.ram[pending_address],"issued handler read returned wrong RAM byte");
                    pending=0;
                end
            end
            if(dbg_request&&dbg_cpu_owned&&!dbg_dma)begin
                check(!pending,"only one CPU request in flight");
                pending=1;pending_address=dbg_cpu_addr;pending_we=dbg_cpu_we;pending_write_data=dbg_cpu_do;
            end
            if(late_mode&&first_fetch_retired&&!first_body_issued&&dbg_request&&!test_cpu_sync)begin
                first_body_issued=1;first_body_address=dbg_cpu_addr;
                check(!dbg_cpu_we,"first handler body cycle is a read");
            end
            if(dbg_retire&&!dbg_rdy&&!dbg_cpu_we)begin
                held_reads++;held_check=1;held_address=dbg_cpu_addr;
                if(test_vic_y<80)badline_holds++;
                else if(test_vic_y<=101&&test_vic_y[2:0]!=3)sprite_holds++;
            end
            // Check real backing writes after the synchronous RAM edge, even
            // if VIC BA is already low. RDY must not discard a started store.
            if(ram_ce&&ram_we&&dbg_cpu_owned&&ram_addr>=2)begin
                write_check=1;saved_write_address=ram_addr;saved_write_data=ram_data;ram_writes++;
            end
            if(dbg_retire&&(dbg_rdy||dbg_cpu_we))begin
                if(late_mode)begin
                    if(!dbg_cpu_we&&dbg_cpu_addr==16'hfffa)begin
                        nmi_vectors++;
                        if(nmi_vectors==2)second_stack_count=stack_count;
                    end
                    if(!dbg_cpu_we&&dbg_cpu_addr==16'hfffb)nmi_vector_highs++;
                    if(dbg_cpu_we&&dbg_cpu_addr[15:8]==8'h01)begin
                        check(stack_count<32,"bounded interrupt stack trace");
                        stack_address[stack_count]=dbg_cpu_addr;stack_data[stack_count]=dbg_cpu_do;stack_count++;
                    end
                    if(!dbg_cpu_we&&test_cpu_sync&&dbg_cpu_addr==16'h0b00&&nmi_vectors==1)
                        first_fetch_retired=1;
                end
                if(dbg_phase!=31)extra_cycles++;
                if(dbg_cpu_we&&!dbg_rdy)ba_writes++;
                if(!dbg_cpu_we&&test_cpu_sync)begin
                    case(dbg_cpu_addr)
                        16'h0a00:irq_entries++;
                        16'h0b00:nmi_entries++;
                        16'h0a05:irq_rti++;
                        16'h0b05:nmi_rti++;
                        default:begin
                            if(dbg_cpu_addr>=16'h0900&&dbg_cpu_addr<16'h0940)begin
                                if(irq_rti)irq_resume=1;
                                if(nmi_rti)nmi_resume=1;
                            end
                        end
                    endcase
                end
                if(dbg_cpu_we&&dbg_cpu_addr==16'h2204&&dbg_cpu_do==8'ha5)finished=1;
            end
        end
        #1;
        if(held_check)check(dbg_cpu_addr===held_address&&!dbg_cpu_we,"RDY-held read changed CPU bus");
        if(write_check)check(memory_i.ram[saved_write_address]===saved_write_data,"accepted machine store did not reach RAM");
    end
    task automatic load_roms;
        @(negedge clk);bios=0;rom_we=1;
        for(integer address=0;address<16384;address++)begin
            rom_addr=address;
            case(address)
                14'h3ffa:rom_data=8'h00;
                14'h3ffb:rom_data=8'h0b;
                14'h3ffc:rom_data=8'h00;
                14'h3ffd:rom_data=8'h08;
                14'h3ffe:rom_data=8'h00;
                14'h3fff:rom_data=8'h0a;
                default:rom_data=8'hea;
            endcase
            @(negedge clk);
        end
        bios=1;
        for(integer address=0;address<4096;address++)begin
            rom_addr=address;rom_data=8'hc7;@(negedge clk);
        end
        rom_we=0;bios=0;
    endtask
    task automatic run_case(input integer selected);
        dense=selected;
        @(negedge clk);running=0;late_mode=0;pending=0;pending_checks=0;reset_n=0;pause=1;irq_n=1;nmi_n=1;
        repeat(160)@(negedge clk);
        check(dbg_reset&&paused,"reset/pause before loading");
        loader_we=1;
        for(integer address=0;address<65536;address++)begin
            loader_addr=address;loader_data=initial_ram[address];@(negedge clk);
        end
        loader_we=0;turbo_mode=dense?2'b10:2'b00;turbo_speed=dense?3:0;
        clocks=0;irq_entries=0;nmi_entries=0;irq_rti=0;nmi_rti=0;held_reads=0;
        badline_holds=0;sprite_holds=0;ba_writes=0;extra_cycles=0;ram_writes=0;
        irq_sent=0;nmi_sent=0;irq_resume=0;nmi_resume=0;finished=0;
        irq_inject_y=-1;nmi_inject_y=-1;reset_n=1;pause=0;running=1;
        // A machine reset clears VIC registers but does not restart its raster
        // counter. Permit one natural wrap before the line51/80 injections.
        while(clocks<800000&&!(finished&&nmi_resume&&test_vic_y>=112))begin
            @(negedge clk);
            // Drive input before a real enabled read attempt held by VIC BA.
            if(!dbg_reset&&dbg_retire&&!dbg_rdy&&!dbg_cpu_we)begin
                if(!irq_sent&&test_vic_y>=48&&test_vic_y<80)begin
                    irq_n=0;irq_sent=1;irq_inject_y=test_vic_y;
                end else if(irq_resume&&!nmi_sent&&test_vic_y>=80&&test_vic_y<=100&&test_vic_y[2:0]!=3)begin
                    nmi_n=0;nmi_sent=1;nmi_inject_y=test_vic_y;
                end
            end
            if(irq_entries>0)irq_n=1;
            if(nmi_entries>0)nmi_n=1;
            clocks++;
        end
        check(clocks<800000&&test_vic_y>=112,"completed full post-interrupt sprite observation window");
        check(finished&&irq_sent&&nmi_sent,"both injected interrupts and resumed program completed");
        check(irq_entries==1&&nmi_entries==1&&irq_rti==1&&nmi_rti==1,"one IRQ/NMI handler and RTI each");
        check(irq_resume&&nmi_resume,"CPU fetched main loop after each RTI");
        check(memory_i.ram[16'h2200]===1&&memory_i.ram[16'h2201]===1,"both handlers executed exactly once");
        check(badline_holds>0&&sprite_holds>0,"real display and sprite windows held CPU reads");
        check(ba_writes>0,"RDY-low write cycles completed during VIC warning window");
        check(ram_writes>100,"continuous RMW traffic reached production RAM");
        check(dense?extra_cycles>0:extra_cycles==0,"requested CPU cadence actually used");
        $display("INTERRUPT_SUMMARY ntsc=%0d dense=%0d irq_line=%0d nmi_line=%0d held_reads=%0d badline_holds=%0d sprite_holds=%0d ba_low_writes=%0d irq_entries=%0d nmi_entries=%0d irq_rti=%0d nmi_rti=%0d ram_writes=%0d extras=%0d clocks=%0d",
            ntsc,dense,irq_inject_y,nmi_inject_y,held_reads,badline_holds,sprite_holds,ba_writes,
            irq_entries,nmi_entries,irq_rti,nmi_rti,ram_writes,extra_cycles,clocks);
        running=0;
    endtask
    task automatic run_late_case(input integer selected,input logic after_issue);
        integer guard_clocks,frame_index;
        dense=selected;ntsc=0;
        @(negedge clk);running=0;late_mode=1;late_after_issue=after_issue;
        pending=0;pending_checks=0;reset_n=0;pause=1;irq_n=1;nmi_n=1;
        repeat(160)@(negedge clk);
        check(dbg_reset&&paused,"late NMI reset/pause before loading");
        $readmemh("late_memory.hex",initial_ram);
        loader_we=1;
        for(integer address=0;address<65536;address++)begin
            loader_addr=address;loader_data=initial_ram[address];@(negedge clk);
        end
        loader_we=0;turbo_mode=dense?2'b10:2'b00;turbo_speed=dense?3:0;
        clocks=0;guard_clocks=0;irq_entries=0;nmi_entries=0;irq_rti=0;nmi_rti=0;
        nmi_vectors=0;nmi_vector_highs=0;stack_count=0;second_stack_count=0;
        first_fetch_retired=0;first_body_issued=0;late_second_sent=0;nmi_sent=0;
        irq_resume=0;nmi_resume=0;finished=0;reset_n=1;pause=0;running=1;
        while(clocks<100000&&guard_clocks<8192)begin
            @(negedge clk);
            if(!dbg_reset&&!nmi_sent&&test_cpu_sync&&dbg_cpu_addr==16'h0900)begin
                nmi_n=0;nmi_sent=1;
            end
            if(nmi_vectors==1&&!late_second_sent)nmi_n=1;
            if(!late_second_sent&&first_fetch_retired&&(!after_issue||first_body_issued))begin
                if(after_issue)check(pending&&!dbg_cpu_we,"late edge injected between real read issue and retirement");
                else check(!first_body_issued&&!pending,"late edge injected before first handler body issue");
                nmi_n=0;late_second_sent=1;
            end
            if(nmi_vectors>=2)nmi_n=1;
            if(finished&&nmi_rti==2&&nmi_resume)guard_clocks++;
            clocks++;
        end
        check(clocks<100000&&guard_clocks==8192,"late NMI completed and remained quiet after both returns");
        check(nmi_sent&&late_second_sent&&first_body_issued,"both NMI edges and body request exercised");
        check(nmi_vectors==2&&nmi_vector_highs==2&&irq_entries==0,"exactly two complete NMI vector pairs");
        check(nmi_rti==2&&memory_i.ram[16'h2201]===2&&nmi_resume,"two complete handlers/RTIs resumed main loop");
        // VICE executes the first handler opcode before a second NMI.
        // The same edge before/after fabric issue belongs to the same CPU
        // clock attempt; it cannot cancel an already fetched PHA.
        // The actual VICE CPU matrix covers this as nmi-double-normal-+10.
        check(first_body_address==16'h0b01,"first handler PHA dummy read keeps its normal address");
        frame_index=4;
        check(second_stack_count==7,"first PHA executes exactly once before second NMI frame");
        check(stack_address[frame_index]==16'h01fb&&stack_data[frame_index]==8'h0b,
            "second NMI pushes expected PC high byte and stack position");
        check(stack_data[frame_index+1]==8'h01,"second NMI returns after the first handler PHA");
        check(stack_data[frame_index+2][4]===1'b0,"second hardware NMI stacks clear B flag");
        check(pending_checks>50,"request-to-retirement pipeline stability exercised");
        $display("LATE_NMI_SUMMARY dense=%0d after_issue=%0d first_body=%04h stack_before_second_vector=%0d vectors=%0d rti=%0d counter=%0d pending_checks=%0d clocks=%0d",
            dense,after_issue,first_body_address,second_stack_count,nmi_vectors,nmi_rti,memory_i.ram[16'h2201],pending_checks,clocks);
        running=0;pending=0;late_mode=0;
    endtask
    initial begin
        $readmemh("machine_memory.hex",initial_ram);load_roms();
        for(integer standard=0;standard<2;standard++)begin
            ntsc=standard;
            for(integer selected=0;selected<2;selected++)run_case(selected);
        end
        for(integer selected=0;selected<2;selected++)begin
            run_late_case(selected,0);
            run_late_case(selected,1);
        end
        $display("PASS: C64 machine interrupt integration PAL/NTSC stock/dense, real VIC steals, IRQ/NMI RTI/resume and RDY-low writes");
        $finish;
    end
    initial begin #40000000;$fatal(1,"FAIL: machine interrupt timeout");end
endmodule

// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module BUFG(input wire I,output wire O);assign O=I;endmodule
// Real guest instructions transfer original, fully formatted/checksummed tracks.
// The only forced state is mechanical ready/spindle state, to avoid spin-up.
module tb_apple_iigs_fast_floppy_protocol;
    reg core_clk=0;always #8.730 core_clk=~core_clk;
    reg reset=1,core_reset_request=1,pause_request=0,clear_load_error=0;
    reg [15:0] input_code=0,input_value=0;reg input_valid=0;wire input_ready;
    reg [7:0] load_data=0,load_kind=0;
    reg load_valid=0,load_first=0,load_last=0,load_epoch=0,load_abort=0,load_abort_epoch=0;
    reg [31:0] load_offset=0,load_total=0;wire load_ready;
    wire [23:0] memory_addr;wire memory_write;wire [63:0] memory_wdata;wire [7:0] memory_wstrb;
    wire memory_valid,memory_ready,memory_burst,memory_busy;reg [63:0] memory_rdata=0;reg memory_rvalid=0,memory_fault=0;
    reg media_mount=0,media_eject=0,media_complete=0;
    reg [7:0] media_slot=0,media_kind=0;reg media_read_only=0;
    reg [31:0] media_image_size=0,media_generation=0;
    reg [31:0] media_complete_request_id=0,media_complete_status=0,media_complete_generation=0;
    reg [15:0] media_complete_block_count=0;
    wire media_request_valid,media_request_write;
    wire [31:0] media_request_id,media_request_lba,media_request_generation;
    wire [15:0] media_request_block_count;
    wire [7:0] media_write_data;wire media_write_data_valid,media_write_data_last;
    reg media_write_data_ready=0;
    wire media_state_mounted,media_state_dirty,media_state_eject_pending;
    wire [31:0] core_id;wire ready,load_error,rom_loaded;
    wire [7:0] video_r,video_g,video_b;wire video_de,video_hsync,video_vsync,video_valid,video_pal;
    wire [9:0] video_x;wire [8:0] video_y;
    wire signed [23:0] audio_l,audio_r;wire audio_valid;
    multitini_apple_iigs_adapter dut(.*);
    reg [63:0] memory[0:2097151];
    reg busy=0,pending_write=0;integer beats_left=0;reg [23:0] pending_addr;reg [63:0] pending_data;reg [7:0] pending_mask;
    integer delay_count=0;
    assign memory_ready=!busy;
    always @(posedge core_clk)begin
        memory_rvalid<=0;
        if(memory_valid && memory_ready)begin
            pending_addr<=memory_addr;pending_data<=memory_wdata;pending_mask<=memory_wstrb;
            pending_write<=memory_write;delay_count<=3+memory_addr[5:3];busy<=1;
            beats_left<=memory_burst ? 4:1;
            if(memory_burst && (memory_write || memory_addr[4:0]!=0))$fatal(1,"invalid DDR burst");
        end else if(busy)begin
            if(delay_count!=0)delay_count<=delay_count-1;
            else begin
                if(pending_write)for(integer lane=0;lane<8;lane=lane+1)
                    if(pending_mask[lane])memory[pending_addr[23:3]][lane*8+:8]<=pending_data[lane*8+:8];
                memory_rdata<=memory[pending_addr[23:3]];memory_rvalid<=1;
                if(beats_left>1)begin beats_left<=beats_left-1;pending_addr<=pending_addr+8;delay_count<=pending_addr[3];end
                else busy<=0;
            end
        end
    end
    reg [7:0] rom[0:131071],hdd_rom[0:2303],stream[0:10239],image[0:65535];
    reg [7:0] written[0:10239],saved[0:10239],shadow[0:10239];
    integer cursor=0,arm_pc=0,write_end_pc=0,kernel_return=0,writer_cursor=0;
    integer family35=1,fast=0,stream_bytes=0,track_bits=0,track_blocks=0,image_bytes=0;
    integer ticks=0,arm_tick=0,write_end_tick=0,read_count=0,write_count=0;
    integer bit_writes=0,position_changes=0,kernel_ops=0,native_edges=0,video_samples=0,audio_samples=0;
    integer drain_phase=-1,drain_stage=0,shadow_bits=0,resume_after_pause=0,transition=-1;
    integer dither_period=1,dither_numerator=0,dither_phase=0,writer_cycles=32,writer_phase_cycles=0;
    integer writer_c036=0,writer_bank_cycles=4,writer_bank_chunk=512;
    integer last_write_tick=0,write_gap_sum=0,write_gap_min=8191,write_gap_max=0;
    integer engine_ticks=0,last_write_engine_tick=0;
    integer cadence_fd=0;
    integer write_gap_hist[0:8191];
    reg armed=0,previous_write_strobe=0,previous_access=0,spec_seen=0;reg [16:0] previous_position=0;
    string hdd_path,disk_path,stream_path,output_path;
`ifdef BASELINE606
    wire expected_tick=dut.phase==3;
`else
    wire expected_tick=dut.floppy_engine_ce;
`endif
    wire write_intent=family35 ?
        (dut.machine_i.iwmc.drive35.WRITE_STROBE && dut.machine_i.iwmc.drive35.WRITE_MODE &&
         !dut.machine_i.iwmc.drive35.WRITE_PROTECT && dut.machine_i.iwmc.drive35.TRACK_LOADED):
        (dut.machine_i.iwmc.drive525.WRITE_STROBE && dut.machine_i.iwmc.drive525.WRITE_MODE &&
         !dut.machine_i.iwmc.drive525.WRITE_PROTECT && dut.machine_i.iwmc.drive525.TRACK_LOADED);
    wire raw_intent_strobe=family35 ? dut.machine_i.iwmc.drive35.WRITE_STROBE:dut.machine_i.iwmc.drive525.WRITE_STROBE;
    wire [15:0] intent_addr=family35 ? dut.machine_i.iwmc.drive35.BRAM_ADDR:dut.machine_i.iwmc.drive525.BRAM_ADDR;
    wire [2:0] intent_shift=family35 ? dut.machine_i.iwmc.drive35.bit_shift:dut.machine_i.iwmc.drive525.bit_shift;
    wire intent_bit=family35 ? dut.machine_i.iwmc.drive35.WRITE_BIT:dut.machine_i.iwmc.drive525.WRITE_BIT;
    wire pending_rmw=family35 ? dut.machine_i.iwmc.drive35.write_strobe_d1:dut.machine_i.iwmc.drive525.write_strobe_d1;
    wire pending_track_write=family35 ? dut.machine_i.iwmc.drive35.WRITE_WE_OUT:dut.machine_i.iwmc.drive525.WRITE_WE_OUT;
    wire [15:0] pending_track_addr=drain_stage==0 ?
        (family35 ? dut.machine_i.iwmc.drive35.write_addr_d1:dut.machine_i.iwmc.drive525.write_addr_d1):
        (family35 ? dut.machine_i.iwmc.drive35.WRITE_ADDR_OUT:dut.machine_i.iwmc.drive525.WRITE_ADDR_OUT);
    wire [7:0] intent_current=family35 ? dut.g_woz[0].floppy_i.track_ram_side0.mem[intent_addr]:
                                       dut.g_woz[1].floppy_i.track_ram_side0.mem[intent_addr];
    wire [7:0] pending_current=family35 ? dut.g_woz[0].floppy_i.track_ram_side0.mem[pending_track_addr]:
                                        dut.g_woz[1].floppy_i.track_ram_side0.mem[pending_track_addr];
    wire pending_changes_track=drain_stage==2 ? intent_current[intent_shift]!=intent_bit:
                                               pending_current!=shadow[pending_track_addr];
`ifdef HAS_DRAIN_HELPERS
    wire expected_drain=family35 ? dut.machine_i.iwmc.drive35.WRITE_DRAIN_ENABLE:
                                        dut.machine_i.iwmc.drive525.WRITE_DRAIN_ENABLE;
    wire trace_accepted=family35 ? dut.machine_i.iwmc.drive35.accepted_write_strobe:
                                         dut.machine_i.iwmc.drive525.accepted_write_strobe;
    wire trace_phase_pending=family35 ? dut.machine_i.iwmc.drive35.drain_phase_pending:
                                              dut.machine_i.iwmc.drive525.drain_phase_pending;
`else
    wire expected_drain=0;
    wire trace_accepted=0,trace_phase_pending=0;
`endif
    task drain_trace(input integer edge_number);
        begin
            $display("DRAIN_TRACE edge=%0d phase=%0d pause=%b CE=%b raw_strobe=%b accepted=%b d1=%b WE=%b intent_addr=%0d shift=%0d bit=%b rmw_addr=%0d rmw_shift=%0d BRAM_ADDR=%0d BRAM_DATA=%02x pos=%0d timer=%0d phase_pending=%b shadow=%02x actual=%02x",
                edge_number,dut.phase,pause_request,expected_tick,write_intent,trace_accepted,pending_rmw,pending_track_write,
                intent_addr,intent_shift,intent_bit,
                family35 ? dut.machine_i.iwmc.drive35.write_addr_d1:dut.machine_i.iwmc.drive525.write_addr_d1,
                family35 ? dut.machine_i.iwmc.drive35.write_shift_d1:dut.machine_i.iwmc.drive525.write_shift_d1,
                intent_addr,family35 ? dut.machine_i.iwmc.drive35.BRAM_DATA:dut.machine_i.iwmc.drive525.BRAM_DATA,
                dut.machine_i.iwmc.current_bit_position,
                family35 ? dut.machine_i.iwmc.drive35.bit_timer:dut.machine_i.iwmc.drive525.bit_timer,
                trace_phase_pending,shadow[pending_track_addr],pending_current);
            $fflush();
        end
    endtask
    task apply_transition;
        begin
`ifndef BASELINE606
            case(transition)
                0:force dut.machine_i.zip_speed_code=3'd3;
                1:force dut.machine_i.io_slow_holdoff=1;
                2:force dut.machine_i.irq_slowdown=1;
                3:force dut.machine_i.iwm_smartport_mode=1;
            endcase
`endif
        end
    endtask
    task release_transition;
        begin
`ifndef BASELINE606
            case(transition)
                0:release dut.machine_i.zip_speed_code;
                1:release dut.machine_i.io_slow_holdoff;
                2:release dut.machine_i.irq_slowdown;
                3:release dut.machine_i.iwm_smartport_mode;
            endcase
`endif
        end
    endtask
    always @(posedge core_clk)begin
        ticks<=ticks+1;
        if(armed)begin
            if(expected_tick)engine_ticks++;
            // Specification oracle: each accepted serial bit changes exactly
            // that reported physical track bit. This does not observe the
            // delayed RMW address, BRAM read data or generated write byte.
            if(!raw_intent_strobe)spec_seen=0;
            if(write_intent && !spec_seen && (expected_tick || expected_drain))begin
                if(intent_addr>=track_blocks*512)$fatal(1,"write intent outside selected track payload");
                shadow[intent_addr]=(shadow[intent_addr]&~(8'd1<<intent_shift)) |
                    ({7'd0,intent_bit}<<intent_shift);
                shadow_bits=shadow_bits+1;
                spec_seen=1;
            end
            if(ticks%1000000==0)begin
                $display("PROTOCOL_PROGRESS ticks=%0d pc=%06x X=%04x Y=%04x bits=%0d mounted=%b eject=%b dirty=%b request=%b write=%b",
                    ticks-arm_tick,dut.machine_i.cpu_addr,dut.machine_i.cpu.X,dut.machine_i.cpu.Y,bit_writes,
                    dut.mounted[family35 ? 2:3],dut.eject_pending[family35 ? 2:3],dut.woz_dirty[family35 ? 0:1],media_request_valid,media_request_write);
                $fflush();
            end
            if(video_valid)video_samples=video_samples+1;
            if(audio_valid)audio_samples=audio_samples+1;
            if(dut.machine_i.iwmc.current_bit_position!=previous_position)position_changes=position_changes+1;
            previous_position=dut.machine_i.iwmc.current_bit_position;
            if(dut.machine_i.iwmc.disk_write_strobe && !previous_write_strobe)bit_writes=bit_writes+1;
            previous_write_strobe=dut.machine_i.iwmc.disk_write_strobe;
            if(dut.machine_i.iwmc.cpu_access_edge && !previous_access && dut.machine_i.iwmc.bus_wr &&
                dut.machine_i.iwmc.immediate_q6 && dut.machine_i.iwmc.immediate_q7 && dut.machine_i.iwmc.iwm_active)begin
                write_count=write_count+1;
                if(!family35)begin
                    if(cadence_fd)$fwrite(cadence_fd,"%0d %0d %0d %0d %0d %0d %02x\n",write_count,ticks-arm_tick,engine_ticks,
                        dut.machine_i.iwmc.iwm.rw_state,dut.machine_i.iwmc.iwm.window_counter,
                        dut.machine_i.iwmc.iwm.write_bit_count,dut.machine_i.iwmc.bus_din);
                    if(write_count<=16)$display("WRITE_CADENCE number=%0d tick=%0d gap=%0d virtual_gap=%0d state=%0d window=%0d bitcount=%0d pos=%0d data=%02x",
                        write_count,ticks-arm_tick,last_write_tick==0 ? 0:ticks-last_write_tick,
                        last_write_tick==0 ? 0:engine_ticks-last_write_engine_tick,
                        dut.machine_i.iwmc.iwm.rw_state,dut.machine_i.iwmc.iwm.window_counter,
                        dut.machine_i.iwmc.iwm.write_bit_count,dut.machine_i.iwmc.current_bit_position,dut.machine_i.iwmc.bus_din);
                    if(last_write_tick!=0)begin
                        if(ticks-last_write_tick<8192)write_gap_hist[ticks-last_write_tick]++;
                        write_gap_sum+=ticks-last_write_tick;
                        if(ticks-last_write_tick<write_gap_min)write_gap_min=ticks-last_write_tick;
                        if(ticks-last_write_tick>write_gap_max)write_gap_max=ticks-last_write_tick;
                    end
                    last_write_tick=ticks;
                    last_write_engine_tick=engine_ticks;
                end
            end
            previous_access=dut.machine_i.iwmc.cpu_access_edge;
            if(dut.machine_i.cpu.EN && dut.machine_i.cpu.STATE==0 && dut.machine_i.cpu_addr[23:16]==2)
                kernel_ops=kernel_ops+1;
        end
    end
    always @(posedge dut.clk14)if(armed)native_edges=native_edges+1;
    task emit(input [7:0] value);begin rom[cursor]=value;cursor++;end endtask
    task immediate(input [7:0] value);begin emit(8'ha9);emit(value);end endtask
    task lda(input [15:0] address);begin emit(8'had);emit(address[7:0]);emit(address[15:8]);end endtask
    task sta(input [15:0] address);begin emit(8'h8d);emit(address[7:0]);emit(address[15:8]);end endtask
    task branch(input [7:0] op,input integer target);
        integer offset;
        begin offset=target-(cursor+2);if(offset< -128 || offset>127)$fatal(1,"branch range");emit(op);emit(offset[7:0]);end
    endtask
    task word(input [15:0] value);begin emit(value[7:0]);emit(value[15:8]);end endtask
    task writer_emit(input [7:0] value);
        begin memory[writer_cursor>>3][(writer_cursor&7)*8+:8]=value;writer_cursor++;end
    endtask
    task writer_delay(input integer cycles);
        integer remaining;
        begin
            remaining=cycles;
            if(remaining<0 || remaining==1)$fatal(1,"unrepresentable guest delay %0d cycles",remaining);
            if(remaining%2==1)begin writer_emit(8'h80);writer_emit(0);remaining-=3;end
            repeat(remaining/2)writer_emit(8'hea);
        end
    endtask
    task build_program;
        integer reader,next_bank,padding,dither;
        begin
            for(integer i=0;i<131072;i++)rom[i]=8'hea;
            rom[131068]=0;rom[131069]=8'he0;cursor=122880;
            emit(8'h78);emit(8'h18);emit(8'hfb);emit(8'hc2);emit(8'h10); // SEI, native mode, 16-bit index.
            immediate(8'h80);sta(16'hc036);immediate(family35 ? 8'h40:0);sta(16'hc031);
            lda(16'hc0e8);lda(16'hc0ea);lda(16'hc0ed);lda(16'hc0ef);
            immediate(family35 ? 15:0);sta(16'hc0ef); // Sony async; DiskII sync4us cells.
            lda(16'hc0ee);lda(16'hc0ec);lda(16'hc0e9);
            arm_pc=cursor;
            emit(8'ha2);word(0);reader=cursor;
            lda(16'hc0ec);branch(8'h10,reader); // Poll data-valid, no imposed byte pacing.
            emit(8'h9f);emit(0);emit(0);emit(3); // STA $030000,X.
            emit(8'he8);emit(8'he0);word(10240);branch(8'hd0,reader);
            if(!family35)begin immediate(writer_c036);sta(16'hc036);end
            lda(16'hc0ed);lda(16'hc0ef); // Enter write mode through real switches.
            emit(8'h5c);emit(0);emit(0);emit(4); // Original unrolled writer in real guest RAM.
            kernel_return=cursor;
            if(family35)begin
                // Sony0D: CA1=1,CA0=1,CA2=1, then a real LSTRB rising edge.
                lda(16'hc0e6);lda(16'hc0e1);lda(16'hc0e3);lda(16'hc0e5);lda(16'hc0e7);lda(16'hc0e6);
            end else lda(16'hc0e8);
            emit(8'hdb);
            // LDY #60000; INC A; DEY; BNE -4; JML $00:kernel_return.
            memory[24'h020000>>3]={8'hfc,8'hd0,8'h88,8'h1a,8'hea,8'h60,8'ha0,8'hea};
            memory[24'h020008>>3]={32'heaeaeaea,8'h00,kernel_return[15:0],8'h5c};
            // Poll + immediate + store fits the stock byte budget. A generic
            // indexed loop adds enough CPU overhead to wrap over its own first
            // sector on the stock 3.5-inch path, so it is not a valid formatter.
            writer_cursor=24'h040000;
            if(!family35)writer_delay(writer_phase_cycles);
            for(integer i=0;i<stream_bytes;i++)begin
                if(family35 && writer_cursor[15:0]>16'hffe0)begin
                    next_bank=(writer_cursor>>16)+1;
                    writer_emit(8'h5c);writer_emit(0);writer_emit(0);writer_emit(next_bank[7:0]);
                    writer_cursor=next_bank<<16;
                end
                if(family35)begin
                    writer_emit(8'had);writer_emit(8'hec);writer_emit(8'hc0); // LDA handshake.
                    writer_emit(8'h10);writer_emit(8'hfb); // BPL back to poll.
                end
                writer_emit(8'ha9);writer_emit(stream[i]);
                writer_emit(8'h8d);writer_emit(8'hed);writer_emit(8'hc0);
                if(!family35)begin
                    // Runtime pacing describes the original guest program,
                    // not a production clock change. Record every actual IWM
                    // store on primary and virtual clocks to prove its rate.
                    dither=((i+dither_phase+1)*dither_numerator)/dither_period-
                           ((i+dither_phase)*dither_numerator)/dither_period;
                    padding=writer_cycles-6-dither-
                        ((i%writer_bank_chunk==writer_bank_chunk-1 && i+1<stream_bytes) ? writer_bank_cycles:0);
                    writer_delay(padding);
                    if(writer_cursor[15:0]>16'hfff0)$fatal(1,"writer chunk exceeds guest bank");
                    if(i%writer_bank_chunk==writer_bank_chunk-1 && i+1<stream_bytes)begin
                        next_bank=(writer_cursor>>16)+1;
                        writer_emit(8'h5c);writer_emit(0);writer_emit(0);writer_emit(next_bank[7:0]);
                        writer_cursor=next_bank<<16;
                    end
                end
            end
            repeat(100)writer_emit(8'hea);
            writer_emit(8'had);writer_emit(8'hee);writer_emit(8'hc0);
            writer_emit(8'had);writer_emit(8'hec);writer_emit(8'hc0);
            if(!family35)begin
                writer_emit(8'ha9);writer_emit(8'h80);writer_emit(8'h8d);writer_emit(8'h36);writer_emit(8'hc0);
            end
            write_end_pc=writer_cursor;
            writer_emit(8'h5c);writer_emit(0);writer_emit(0);writer_emit(2);
        end
    endtask
    task packet(input [7:0] kind,input integer size,input integer speed,input integer lba);
        reg [7:0] value;
        begin
            load_epoch=~load_epoch;
            for(integer i=0;i<size;i++)begin
                value=kind==8'h41 ? rom[i]:kind==8'h49 ? hdd_rom[i]:kind==8'h48 ? image[lba*512+i]:
                    kind==8'h43 && i==1 ? speed:kind==8'h43 && i==3 ? (fast ? 8'h90:8'h10):0;
                @(negedge core_clk);load_kind=kind;load_total=size;load_offset=i;load_data=value;
                load_first=i==0;load_last=i==size-1;load_valid=1;
                while(!load_ready)@(negedge core_clk);
                @(negedge core_clk);load_valid=0;
            end
            repeat(40)@(negedge core_clk);
            if(load_error)$fatal(1,"runtime packet rejected kind=%x",kind);
        end
    endtask
    task complete(input [31:0] status);
        begin
            @(negedge core_clk);media_complete_request_id=media_request_id;
            media_complete_generation=media_request_generation;media_complete_status=status;
            media_complete_block_count=status==0 ? media_request_block_count:0;media_complete=1;
            @(negedge core_clk);media_complete=0;
        end
    endtask
    task mount;
        integer timeout,requests;
        begin
            @(negedge core_clk);media_slot=family35 ? 2:3;media_kind=8'h41;media_image_size=image_bytes;
            media_generation=32'h6b;media_read_only=0;media_mount=1;
            @(negedge core_clk);media_mount=0;timeout=0;requests=0;
            while(!(dut.woz_ready[family35 ? 0:1] && !dut.woz_busy[family35 ? 0:1]) && timeout<2000000)begin
                @(negedge core_clk);timeout++;
                if(media_request_valid)begin
                    if(media_request_write || media_request_generation!=32'h6b ||
                        media_request_block_count!=(family35 ? 16'h0202:16'h0302) ||
                        media_request_lba>=image_bytes/512)$fatal(1,"mount request mismatch");
                    packet(8'h48,512,0,media_request_lba);complete(0);requests++;
                    while(media_request_valid)@(negedge core_clk);
                end
            end
            if(timeout==2000000 || !dut.machine_i.DISK_READY[family35 ? 2:0] || requests<track_blocks+3)
                $fatal(1,"formatted image did not mount");
        end
    endtask
    task dump_tracks;
        integer fd;
        begin
            fd=$fopen({output_path,"-written.bin"},"wb");
            if(!fd)$fatal(1,"cannot open captured track");
            for(integer i=0;i<10240;i++)begin
                written[i]=family35 ? dut.g_woz[0].floppy_i.track_ram_side0.mem[i]:dut.g_woz[1].floppy_i.track_ram_side0.mem[i];
                saved[i]=i<track_blocks*512 ? image[1536+i]:written[i];
                $fwrite(fd,"%c",written[i]);
            end
            $fclose(fd);
            fd=$fopen({output_path,"-read.bin"},"wb");
            if(!fd)$fatal(1,"cannot open guest receive buffer");
            for(integer i=0;i<10240;i++)$fwrite(fd,"%c",memory[(24'h030000+i)>>3][((24'h030000+i)&7)*8+:8]);
            $fclose(fd);
        end
    endtask
    task flush_and_eject;
        integer timeout,requests,retry_lba,sector,fd;
        reg failed;
        begin
            if(!family35 || drain_phase>=0)begin
                @(negedge core_clk);media_slot=family35 ? 2:3;media_eject=1;
                @(negedge core_clk);media_eject=0;
            end
            timeout=0;requests=0;failed=0;retry_lba=-1;
            while(dut.mounted[family35 ? 2:3] && timeout<3000000)begin
                @(negedge core_clk);timeout++;
                if(media_request_valid)begin
                    if(!media_request_write || media_request_generation!=32'h6b ||
                        media_request_block_count!=(family35 ? 16'h0202:16'h0302) ||
                        media_request_lba<3 || media_request_lba>=3+track_blocks)
                        $fatal(1,"durable save request mismatch lba=%0d",media_request_lba);
                    if(failed && retry_lba>=0 && media_request_lba!=retry_lba)$fatal(1,"failed dirty block was not retried first");
                    sector=media_request_lba-3;
                    for(integer i=0;i<512;i++)begin
                        while(!media_write_data_valid)@(negedge core_clk);
                        if(media_write_data!==written[sector*512+i] || media_write_data_last!=(i==511))
                            $fatal(1,"save data lost/repeated byte lba=%0d byte=%0d got=%02x expected=%02x",
                                media_request_lba,i,media_write_data,written[sector*512+i]);
                        saved[sector*512+i]=media_write_data;
                        repeat((i%7)+1)@(negedge core_clk); // Variable host backpressure.
                        media_write_data_ready=1;@(negedge core_clk);media_write_data_ready=0;
                    end
                    if(!failed)begin
                        retry_lba=media_request_lba;failed=1;complete(4);
                        // Retained floppy writes deliberately keep VALID and
                        // ACK asserted across failure; a fresh ID restarts data.
                        while(dut.retry_delay!=0)@(negedge core_clk);
                    end else begin
                        retry_lba=-1;complete(0);
                        while(media_request_valid)@(negedge core_clk);
                    end
                    requests++;
                end
            end
            repeat(40)@(negedge core_clk);
            if(timeout==3000000 || requests<2 || dut.woz_dirty[family35 ? 0:1] ||
                dut.eject_pending[family35 ? 2:3] || dut.machine_i.DISK_READY[family35 ? 2:0])
                $fatal(1,"dirty media did not finish retry/flush/eject");
            fd=$fopen({output_path,"-saved.bin"},"wb");
            for(integer i=0;i<10240;i++)$fwrite(fd,"%c",saved[i]);
            $fclose(fd);
            $display("PROTOCOL_RESULT durable_requests=%0d retry_completed=1 eject_completed=1",requests);
        end
    endtask
    initial begin
        integer fd,timeout,start_ops,start_steps,start_native,start_video,start_audio,elapsed;
        reg [16:0] paused_position;
        if($value$plusargs("FAMILY35=%d",family35))begin end
        if($value$plusargs("FAST=%d",fast))begin end
        if($value$plusargs("DRAIN_PHASE=%d",drain_phase))begin end
        if($value$plusargs("DRAIN_STAGE=%d",drain_stage))begin end
        if($value$plusargs("RESUME_AFTER_PAUSE=%d",resume_after_pause))begin end
        if($value$plusargs("TRANSITION=%d",transition))begin end
        if($value$plusargs("DITHER_PERIOD=%d",dither_period))begin end
        if($value$plusargs("DITHER_NUMERATOR=%d",dither_numerator))begin end
        if($value$plusargs("DITHER_PHASE=%d",dither_phase))begin end
        if($value$plusargs("WRITER_CYCLES=%d",writer_cycles))begin end
        if($value$plusargs("WRITER_PHASE_CYCLES=%d",writer_phase_cycles))begin end
        if($value$plusargs("WRITER_C036=%d",writer_c036))begin end
        if($value$plusargs("WRITER_BANK_CYCLES=%d",writer_bank_cycles))begin end
        if($value$plusargs("WRITER_BANK_CHUNK=%d",writer_bank_chunk))begin end
        if(dither_period<1 || dither_numerator<0 || dither_numerator>=dither_period ||
           dither_phase<0 || writer_cycles<12 || writer_cycles>128 || writer_bank_chunk<1 || writer_bank_chunk>1000 ||
           writer_bank_cycles<0 || writer_bank_cycles>8 || (writer_c036!=0 && writer_c036!=128))
            $fatal(1,"invalid explicit writer pacing");
        for(integer i=0;i<8192;i++)write_gap_hist[i]=0;
        if(!$value$plusargs("STREAM_BYTES=%d",stream_bytes) || !$value$plusargs("TRACK_BITS=%d",track_bits) ||
           !$value$plusargs("TRACK_BLOCKS=%d",track_blocks))$fatal(1,"missing fixture geometry");
        if(!$value$plusargs("HDD_ROM=%s",hdd_path) || !$value$plusargs("DISK=%s",disk_path) ||
           !$value$plusargs("STREAM=%s",stream_path) || !$value$plusargs("OUTPUT=%s",output_path))
            $fatal(1,"missing original fixture inputs");
        if(!family35)begin cadence_fd=$fopen({output_path,"-stores.txt"},"w");if(!cadence_fd)$fatal(1,"cannot open exact store trace");end
        $readmemh(hdd_path,hdd_rom);$readmemh(stream_path,stream);
        fd=$fopen(disk_path,"rb");if(!fd)$fatal(1,"cannot open original WOZ fixture");
        image_bytes=$fread(image,fd);$fclose(fd);build_program();
        repeat(20)@(negedge core_clk);reset=0;
        packet(8'h43,16,0,0);packet(8'h41,131072,0,0);packet(8'h42,4096,0,0);
        packet(8'h46,4096,0,0);packet(8'h49,2304,0,0);mount();
        for(integer i=0;i<10240;i++)shadow[i]=i<track_blocks*512 ? image[1536+i]:0;
        if(family35)begin
            force dut.machine_i.iwmc.drive35.motor_spinning=1'b1;
            force dut.machine_i.iwmc.drive35.drive_ready=1'b1;
            force dut.machine_i.iwmc.drive35.prev_drive_ready=1'b1;
        end else begin
            force dut.machine_i.iwmc.motor_spinning=1'b1;
            force dut.machine_i.iwmc.drive525.drive_ready=1'b1;
            force dut.machine_i.iwmc.drive525.prev_drive_ready=1'b1;
        end
        core_reset_request=0;timeout=0;
        while(!(dut.machine_i.cpu_addr=={8'h00,arm_pc[15:0]} && dut.machine_i.cpu.STATE==0) && timeout<2000000)begin
            @(negedge core_clk);timeout++;
        end
        if(timeout==2000000)$fatal(1,"guest setup did not reach read marker");
        pause_request=1;packet(8'h43,16,6,0);arm_tick=ticks;armed=1;pause_request=0;
        timeout=0;
        if(drain_phase>=0)begin
            while(!(write_count>=64 && dut.phase==drain_phase && pending_changes_track &&
                    (drain_stage==0 ? pending_rmw:drain_stage==1 ? pending_track_write:(write_intent && !spec_seen))) && timeout<100000000)begin
                @(negedge core_clk);timeout++;
            end
            if(timeout==100000000)$fatal(1,"requested drain stage/phase did not occur");
            pause_request=1;paused_position=dut.machine_i.iwmc.current_bit_position;
            if(transition>=0)apply_transition();
            drain_trace(0);
            for(integer e=1;e<=4;e++)begin @(negedge core_clk);drain_trace(e);end
            repeat(12)@(negedge core_clk);
`ifndef BASELINE606
            if(transition>=0 && dut.fast_floppy_active)$fatal(1,"eligibility transition did not restore native mode");
`endif
            if(dut.machine_i.iwmc.current_bit_position!=(drain_stage==2 ?
                    (paused_position+1>=track_bits ? 0:paused_position+1):paused_position))
                $fatal(1,"paused media advanced beyond the accepted serial bit");
            for(integer i=0;i<track_blocks*512;i++)
                if((family35 ? dut.g_woz[0].floppy_i.track_ram_side0.mem[i]:
                               dut.g_woz[1].floppy_i.track_ram_side0.mem[i])!==shadow[i])
                    $fatal(1,"pending serial bit was lost/repeated on pause at byte=%0d phase=%0d stage=%0d actual=%02x expected=%02x",
                        i,drain_phase,drain_stage,
                        family35 ? dut.g_woz[0].floppy_i.track_ram_side0.mem[i]:dut.g_woz[1].floppy_i.track_ram_side0.mem[i],shadow[i]);
            if(transition>=0)begin
                release_transition();repeat(8)@(negedge core_clk);
`ifndef BASELINE606
                if(!dut.fast_floppy_active)$fatal(1,"eligibility transition did not resume fast mode");
`endif
                $display("PROTOCOL_RESULT eligibility_transition=%0d native_and_fast_observed=1",transition);
            end
            if(!resume_after_pause)begin
                dump_tracks();flush_and_eject();
                $display("PROTOCOL_RESULT drain_phase=%0d drain_stage=%0d accepted_serial_bits=%0d paused_flush=1",
                    drain_phase,drain_stage,shadow_bits);
                $display("PASS: paused final-bit drain and durable eject");$finish;
            end else begin
                $display("PROTOCOL_RESULT drain_phase=%0d drain_stage=%0d accepted_serial_bits=%0d resume_after_pause=1",
                    drain_phase,drain_stage,shadow_bits);
                pause_request=0;
            end
        end
        while(!(dut.machine_i.cpu_addr==write_end_pc[23:0] && dut.machine_i.cpu.STATE==0) && timeout<100000000)begin
            @(negedge core_clk);timeout++;
        end
        if(timeout==100000000)$fatal(1,"guest read/write did not complete pc=%06x X=%04x",dut.machine_i.cpu_addr,dut.machine_i.cpu.X);
        pause_request=1;write_end_tick=ticks;dump_tracks();
        if(cadence_fd)begin $fclose(cadence_fd);cadence_fd=0;end
        if(!dut.woz_dirty[family35 ? 0:1] || bit_writes<stream_bytes*8 || write_count!=stream_bytes)
            $fatal(1,"guest serial write coverage absent/repeated bits=%0d stream=%0d data_writes=%0d",bit_writes,stream_bytes,write_count);
        pause_request=0;
        while(dut.machine_i.cpu_addr[23:16]!=2)@(negedge core_clk);
        while(dut.machine_i.iwm_holdoff!=0)@(negedge core_clk);
        start_ops=kernel_ops;start_steps=position_changes;start_native=native_edges;
        start_video=video_samples;start_audio=audio_samples;
        repeat(100000)@(negedge core_clk);
        if(kernel_ops-start_ops<30000 || dut.machine_i.cpu_addr[23:16]!=2)
            $fatal(1,"selected-spindle pure CPU kernel did not stay accelerated dispatches=%0d pc=%06x",kernel_ops-start_ops,dut.machine_i.cpu_addr);
        if(native_edges-start_native<24999 || native_edges-start_native>25001 ||
            video_samples-start_video<24999 || video_samples-start_video>25001 ||
            audio_samples-start_audio<82 || audio_samples-start_audio>85)$fatal(1,"shared clock/sample cadence changed");
        $display("PROTOCOL_RESULT family35=%0d fast=%0d transfer_primary_ticks=%0d stream_bytes=%0d bit_writes=%0d selected_kernel_ops=%0d selected_position_changes=%0d kernel_primary_ticks=100000 native_edges=%0d video_samples=%0d audio_samples=%0d",
            family35,fast,write_end_tick-arm_tick,stream_bytes,bit_writes,kernel_ops-start_ops,position_changes-start_steps,
            native_edges-start_native,video_samples-start_video,audio_samples-start_audio);
        if(!family35)begin
            $display("PROTOCOL_RESULT sync_write_count=%0d gap_sum=%0d gap_min=%0d gap_max=%0d c036=%0d cpu_cycles=%0d dither_num=%0d dither_period=%0d dither_phase=%0d initial_cycles=%0d bank_cycles=%0d bank_chunk=%0d",
                write_count,write_gap_sum,write_gap_min,write_gap_max,writer_c036,writer_cycles,dither_numerator,dither_period,dither_phase,writer_phase_cycles,writer_bank_cycles,writer_bank_chunk);
            for(integer i=0;i<8192;i++)if(write_gap_hist[i]!=0)
                $display("PROTOCOL_WRITE_GAP primary_ticks=%0d occurrences=%0d",i,write_gap_hist[i]);
        end
        flush_and_eject();
        @(negedge core_clk);core_reset_request=1;
        repeat(16)@(negedge core_clk);core_reset_request=0;
        repeat(20000)@(negedge core_clk);
        if(dut.mounted[family35 ? 2:3] || dut.eject_pending[family35 ? 2:3] || load_error)
            $fatal(1,"restart after eject retained stale media/error");
        $display("PASS: actual CPU formatted floppy read/write and durable eject");$finish;
    end
    initial begin #3000000000;$fatal(1,"formatted floppy protocol timeout");end
endmodule

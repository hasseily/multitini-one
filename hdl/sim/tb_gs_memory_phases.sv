// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Optional1 ps propagation makes the child edge follow parent FF updates.
module BUFG(input wire I,output wire O);
    wire delayed;bit model_delay;
    initial model_delay=$test$plusargs("DELAYED_BUFG");
    assign #0.001 delayed=I;assign O=model_delay ? delayed:I;
endmodule
// Sweep DDR response receipt phases with real accelerated CPU writes. Writes
// are posted (memory interface v2): each byte retires exactly when the cache
// enqueues it, reaches DDR in exactly one accepted write lane (bytes of one
// word may merge), and every DDR response arrives only on the selected phase.
module tb_gs_memory_phases;
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
    localparam integer WORDS=16;
    localparam [23:0] BASE=24'h020000;
    reg [63:0] memory[0:2097151];
    reg busy=0,pending_write=0,pending_burst=0;
    integer beats_left=0,write_responses=0;
    reg [23:0] pending_addr;
    reg [63:0] pending_data;
    reg [7:0] pending_mask;
    integer delay_count=0,request_number=0,response_phase=0,test_speed=6;
    integer accepts[0:31],responses[0:31],advances[0:31],retirements[0:31];
    integer receipt_phases[0:3];
    integer race_count=0,mismatch_count=0;
    integer pause_assert_phase=-1,pause_release_phase=-1,pause_state=0,pause_events=0,pause_ticks=0;
    integer native_hold_checks=0,pause_hold_checks=0,late_pause_advances=0;
    bit response_on_phase3=0;
    reg [23:0] response_addr;
    reg [15:0] response_cpu_state;
    assign memory_ready=!busy;
    function automatic integer byte_index(input [23:0] address);
        return ((address-BASE)>>3)*2+address[0];
    endfunction
    function automatic bit target_address(input [23:0] address);
        return address>=BASE && address<BASE+WORDS*8 && address[2:0]<2;
    endfunction
    function automatic [7:0] target_value(input [23:0] address);
        reg [15:0] value;
        begin value=16'h4000+((address-BASE)>>3)*16'h0101;return address[0] ? value[15:8] : value[7:0];end
    endfunction
    function automatic [255:0] native_cpu_state;
        return {dut.machine_i.cpu.A,dut.machine_i.cpu.X,dut.machine_i.cpu.Y,dut.machine_i.cpu.SP,
            dut.machine_i.cpu.PC,dut.machine_i.cpu.D,dut.machine_i.cpu.PBR,dut.machine_i.cpu.DBR,
            dut.machine_i.cpu.P,dut.machine_i.cpu.STATE,dut.machine_i.cpu.IR,
            dut.machine_i.cpu.A_OUT,dut.machine_i.cpu.D_OUT,dut.machine_i.cpu.WE,
            dut.machine_i.cpu.VPA,dut.machine_i.cpu.VDA};
    endfunction
    // Raw control changes precede the selected parent edge by half a period.
    // External CPU pause holds primary-clock retirement immediately. Assert
    // while a real write response is outstanding, at either publication phase.
    // PAUSE_KIND=1 instead pauses a target read-back stalled on its DDR read
    // (a line fill, or a single-beat read in cache-bypass mode), and
    // PAUSE_KIND=2 a read stalled until the target's posted writes drain.
    // Those kinds interleave the program per line: four stores, then their
    // four read-backs, newest first, after the program's code lines were
    // read into the cache. With the cache on, each line's first read is of
    // the word just written, so it always waits for that line's posted
    // writes. In bypass mode every instruction fetch is a DDR read, so the
    // drain falls on the fetch after a store; kind 2 pauses that fetch. Kind
    // 2 adds ten cycles of DDR latency, so the drain spans every phase. A
    // paused target read must return the stored byte.
    integer pause_kind=0,read_pause_advances=0,read_pause_mismatches=0,read_pause_mode_errors=0;
    reg [23:0] pause_addr=0;reg read_done=0;
    wire stalled_any=dut.access_valid && !dut.bus_write && dut.cache_stall;
    wire stalled_read=stalled_any && target_address(dut.physical_addr);
    bit bypass_run;initial bypass_run=$test$plusargs("CACHE_BYPASS");
    wire stalled_drain=(stalled_read || bypass_run && dut.cache_disable && stalled_any) &&
        (dut.cache_i.wb_count!=0 || dut.cache_i.txn_write);
    always @(negedge core_clk)begin
        if(pause_assert_phase>=0 && !dut.machine_reset && pause_events<4)begin
            case(pause_state)
                // Posted target write still in flight to DDR at the chosen phase.
                0:if(dut.phase==pause_assert_phase && (pause_kind==0 ?
                     (dut.cache_i.state!=0 && dut.cache_i.txn_write && dut.memory_addr==BASE+pause_events*32) :
                     pause_kind==1 ? (stalled_read && dut.cache_i.state!=0 && !dut.cache_i.txn_write && !dut.cache_i.owner_loader) :
                     stalled_drain))begin
                    pause_request=1;pause_state=1;pause_ticks=0;pause_addr=dut.physical_addr;read_done=0;
                    if(pause_kind!=0 && dut.cache_disable!==$test$plusargs("CACHE_BYPASS"))
                        read_pause_mode_errors=read_pause_mode_errors+1;
                    $display("PAUSE_ASSERT kind=%0d phase=%0d address=%x native_hold=%0b",pause_kind,dut.phase,
                        pause_kind==0 ? dut.memory_addr : dut.physical_addr,dut.native_stall_q);
                end
                1:begin
                    pause_ticks=pause_ticks+1;
                    if(pause_ticks>=20 && dut.phase==pause_release_phase)begin
                        pause_request=0;pause_state=2;
                        $display("PAUSE_RELEASE phase=%0d address=%x native_hold=%0b",dut.phase,dut.physical_addr,dut.native_stall_q);
                    end
                end
                2:if(pause_kind==0 ? advances[pause_events*8]!=0 : read_done)begin pause_events=pause_events+1;pause_state=0;end
                default:$fatal(1,"invalid pause state");
            endcase
        end
    end
    // Transactions are accepted on primary rising edges. Responses are driven
    // half a period early and consumed only at the selected pre-edge phase.
    always @(core_clk)begin
      if(core_clk)begin
        response_on_phase3=0;
        if(memory_valid && memory_ready)begin
            pending_addr<=memory_addr;pending_data<=memory_wdata;pending_mask<=memory_wstrb;
            pending_write<=memory_write;delay_count<=2+request_number%5+(pause_kind==2 ? 10 : 0);
            request_number<=request_number+1;busy<=1;
            pending_burst<=memory_burst;beats_left<=memory_burst ? 4 : 1;
            if(memory_burst && (memory_write || memory_addr[4:0]!=0))$fatal(1,"invalid DDR burst %x",memory_addr);
            if(!dut.machine_reset && memory_write && memory_addr>=BASE && memory_addr<BASE+WORDS*8)begin
                for(integer lane=0;lane<8;lane=lane+1)if(memory_wstrb[lane])begin
                    if(lane>1)$fatal(1,"unexpected write lane");
                    accepts[byte_index(memory_addr+lane)]=accepts[byte_index(memory_addr+lane)]+1;
                    // A posted or merged lane must carry the CPU's final byte.
                    if(memory_wdata[lane*8+:8]!==target_value(memory_addr+lane))begin
                        mismatch_count=mismatch_count+1;
                        $display("DATA_MISMATCH address=%x lane=%x expected=%x",memory_addr+lane,memory_wdata[lane*8+:8],target_value(memory_addr+lane));
                    end
                end
            end
        end
        if(dut.cache_i.enq && target_address(dut.cache_i.enq_addr))
            retirements[byte_index(dut.cache_i.enq_addr)]=retirements[byte_index(dut.cache_i.enq_addr)]+1;
        if(memory_rvalid && dut.cache_i.state==2 && !dut.cache_i.owner_loader && dut.cache_i.txn_write &&
           dut.memory_addr>=BASE && dut.memory_addr<BASE+WORDS*8)begin
            for(integer lane=0;lane<2;lane=lane+1)if(dut.memory_wstrb[lane])
                responses[byte_index(dut.memory_addr+lane)]=responses[byte_index(dut.memory_addr+lane)]+1;
            receipt_phases[dut.phase]=receipt_phases[dut.phase]+1;write_responses=write_responses+1;
            if(dut.phase==3)begin
                response_on_phase3=1;response_addr=dut.memory_addr;response_cpu_state=dut.machine_i.cpu.STATE;
            end
        end
      end else begin
        memory_rvalid<=0;
        if(busy)begin
            if(delay_count!=0)delay_count<=delay_count-1;
            else if(dut.machine_reset || dut.phase==response_phase)begin
                if(pending_write)for(integer lane=0;lane<8;lane=lane+1)
                    if(pending_mask[lane])memory[pending_addr[23:3]][lane*8+:8]<=pending_data[lane*8+:8];
                memory_rdata<=memory[pending_addr[23:3]];memory_rvalid<=1;
                // Later burst beats also wait for the selected response phase.
                if(beats_left>1)begin beats_left<=beats_left-1;pending_addr<=pending_addr+8;delay_count<=pending_addr[3];end
                else busy<=0;
            end
        end
      end
    end
    always @(posedge core_clk)begin
        reg [255:0] held;
        if(!dut.machine_reset && dut.machine_i.cpu.CE && dut.machine_i.cpu.RDY_IN && dut.bus_write &&
           dut.cpu_valid && target_address(dut.physical_addr))begin
            advances[byte_index(dut.physical_addr)]=advances[byte_index(dut.physical_addr)]+1;
            if(pause_request)late_pause_advances=late_pause_advances+1;
            // The write retires on exactly the edge that enqueues it.
            if(!dut.cache_i.bus_commit || !dut.cache_i.enq || dut.cache_i.enq_addr!=dut.physical_addr)begin
                race_count=race_count+1;
                $display("NATIVE_COMMIT_RACE response=%x CPUwrite=%x state=%0d committed_bus=%0b cache_stall=%0b",
                    response_addr,dut.physical_addr,dut.machine_i.cpu.STATE,dut.committed_bus,dut.cache_stall);
            end
        end
        // A paused CPU must not retire anything; the stalled read retires
        // exactly once after release.
        if(!dut.machine_reset && pause_kind!=0 && pause_request && dut.cpu_ce)read_pause_advances=read_pause_advances+1;
        if(!dut.machine_reset && pause_kind!=0 && pause_state==2 && dut.cpu_ce && !dut.bus_write && dut.physical_addr==pause_addr)begin
            read_done=1;
            if(target_address(pause_addr) && dut.cpu_read_data!==target_value(pause_addr))begin
                read_pause_mismatches=read_pause_mismatches+1;
                $display("READ_PAUSE_MISMATCH address=%x got=%x expected=%x",pause_addr,dut.cpu_read_data,target_value(pause_addr));
            end
        end
        if(!reset && !dut.machine_reset_raw && !dut.machine_reset && !dut.machine_i.cpu.CE)begin
            if(dut.machine_i.cpu.EN)$fatal(1,"primary CPU hold left CPU enabled");
            held=native_cpu_state();native_hold_checks=native_hold_checks+1;
            if(pause_state!=0)pause_hold_checks=pause_hold_checks+1;
            #0.001;
            if(native_cpu_state()!==held)$fatal(1,"CPU architectural/bus state changed under primary hold");
        end
    end
    reg [7:0] rom[0:131071],hdd_rom[0:255];integer cursor,expected_pc;
    string rom_path;
    task emit(input [7:0] value);begin rom[cursor]=value;cursor=cursor+1;end endtask
    task program_rom;
        integer value,address;
        begin
            for(integer i=0;i<131072;i=i+1)rom[i]=8'hea;
            rom[131068]=0;rom[131069]=8'he0;cursor=122880;
            emit(8'h78);emit(8'ha9);emit(8'h80);emit(8'h8d);emit(8'h36);emit(8'hc0);
            if($test$plusargs("CACHE_BYPASS"))begin
                emit(8'ha9);emit(8'h50);repeat(4)begin emit(8'h8d);emit(8'h5a);emit(8'hc0);end
                emit(8'ha9);emit(8'hf7);emit(8'h8d);emit(8'h59);emit(8'hc0);
            end
            emit(8'h18);emit(8'hfb);emit(8'h18);emit(8'hc2);emit(8'h30);
            emit(8'ha2);emit(8'hff);emit(8'h01);emit(8'h9a);
            emit(8'ha2);emit(0);emit(0);emit(8'ha0);emit(0);emit(0);emit(8'h58);
            // Read pauses: load the program's first KiB of code into the
            // cache, so a later stalled read is a data read, not a fetch.
            if(pause_kind!=0)for(integer i=0;i<32;i=i+1)begin
                emit(8'had);emit(8'h00+i*32);emit(8'he0+i/8);
            end
            // The optional second suite allocates every destination in the
            // read cache first, exercising both byte merges into valid hits.
            if($test$plusargs("PRIME_CACHE"))for(integer i=0;i<WORDS;i=i+1)begin
                address=BASE+i*8;
                emit(8'haf);emit(address);emit(address>>8);emit(address>>16);
            end
            // All stores then all read-backs, or per line (read-backs newest
            // first) for read pauses.
            for(integer group=0;group<(pause_kind!=0 ? 4 : 1);group=group+1)begin
                integer first,count;
                first=pause_kind!=0 ? group*4 : 0;count=pause_kind!=0 ? 4 : WORDS;
                for(integer i=first;i<first+count;i=i+1)begin
                    value=16'h4000+i*16'h0101;address=BASE+i*8;
                    emit(8'ha9);emit(value);emit(value>>8);
                    emit(8'h8f);emit(address);emit(address>>8);emit(address>>16);
                end
                for(integer j=0;j<count;j=j+1)begin
                    integer i;
                    i=pause_kind!=0 ? first+count-1-j : first+j;
                    value=16'h4000+i*16'h0101;address=BASE+i*8;
                    emit(8'haf);emit(address);emit(address>>8);emit(address>>16);
                    emit(8'hc9);emit(value);emit(value>>8);emit(8'hf0);emit(1);emit(8'hdb);
                end
            end
            emit(8'hdb);expected_pc=16'he000+cursor-122880;
        end
    endtask
    task packet(input [7:0] kind,input integer size);
        reg [7:0] value;
        begin
            load_epoch=~load_epoch;
            for(integer i=0;i<size;i=i+1)begin
                value=kind==8'h41 ? rom[i]:kind==8'h45 ? hdd_rom[i]:kind==8'h43 && i==1 ? test_speed:0;
                @(negedge core_clk);load_kind=kind;load_total=size;load_offset=i;load_data=value;
                load_first=i==0;load_last=i==size-1;load_valid=1;
                while(!load_ready)@(negedge core_clk);
                @(negedge core_clk);load_valid=0;
            end
            repeat(40)@(negedge core_clk);
            if(load_error)$fatal(1,"runtime load rejected kind=%x",kind);
        end
    endtask
    initial begin
        integer polls,failures;
        if(!$value$plusargs("RESPONSE_PHASE=%d",response_phase) || response_phase<0 || response_phase>3)
            $fatal(1,"response phase0..3 required");
        if(!$value$plusargs("HDD_ROM=%s",rom_path))$fatal(1,"licensed HDD ROM required");
        if($value$plusargs("SPEED=%d",test_speed) && (test_speed<0 || test_speed>6))$fatal(1,"invalid configured speed");
        if($value$plusargs("PAUSE_KIND=%d",pause_kind) && (pause_kind<0 || pause_kind>2))$fatal(1,"invalid pause kind");
        if($value$plusargs("PAUSE_ASSERT_PHASE=%d",pause_assert_phase))begin
            if(!$value$plusargs("PAUSE_RELEASE_PHASE=%d",pause_release_phase) ||
               pause_assert_phase<2 || pause_assert_phase>3 || pause_release_phase<2 || pause_release_phase>3)
                $fatal(1,"pause assertion/release phases2 or3 required");
        end
        $readmemh(rom_path,hdd_rom);
        for(integer i=0;i<32;i=i+1)begin accepts[i]=0;responses[i]=0;advances[i]=0;retirements[i]=0;end
        for(integer i=0;i<4;i=i+1)receipt_phases[i]=0;
        for(integer i=0;i<WORDS;i=i+1)memory[(BASE+i*8)>>3]=64'hbcbcbcbcbcbcbcbc;
        repeat(20)@(negedge core_clk);reset=0;
        program_rom();packet(8'h43,16);packet(8'h41,131072);
        packet(8'h42,4096);packet(8'h46,4096);packet(8'h45,256);
        if(!rom_loaded)$fatal(1,"synthetic ROMs not ready");
        core_reset_request=0;polls=0;
        while(!dut.machine_i.cpu.STPExec && polls<200000)begin @(negedge core_clk);polls=polls+1;end
        repeat(16)@(negedge core_clk);failures=0;
        // Posted writes: DDR is compared only after the cache drained.
        polls=0;while(memory_busy && polls<100000)begin @(negedge core_clk);polls=polls+1;end
        if(memory_busy)begin failures=failures+1;$display("DRAIN_ERROR posted writes never drained");end
        // The last read-back is word 15, or word 12 for newest-first lines.
        if(!dut.machine_i.cpu.STPExec || dut.machine_i.cpu.PC!=expected_pc ||
           dut.machine_i.cpu.A!=(pause_kind!=0 ? 16'h4c0c : 16'h4f0f) ||
           dut.machine_i.cpu.X!=0 || dut.machine_i.cpu.Y!=0 || dut.machine_i.cpu.P!=9'h003 || load_error)begin
            failures=failures+1;$display("CPU_FINAL_ERROR PC=%x expected=%x A=%x P=%x",dut.machine_i.cpu.PC,expected_pc,dut.machine_i.cpu.A,dut.machine_i.cpu.P);
        end
        for(integer i=0;i<WORDS;i=i+1)begin
            if(memory[(BASE+i*8)>>3]!== (64'hbcbcbcbcbcbc0000 | (16'h4000+i*16'h0101)))begin
                failures=failures+1;$display("RAM_ERROR word=%0d actual=%x",i,memory[(BASE+i*8)>>3]);
            end
            for(integer lane=0;lane<2;lane=lane+1)begin
                integer index;index=i*2+lane;
                if(accepts[index]!=1 || responses[index]!=1 || advances[index]!=1 || retirements[index]!=1)begin
                    failures=failures+1;$display("COUNT_ERROR address=%x accept=%0d response=%0d CPUadvance=%0d retire=%0d",
                        BASE+i*8+lane,accepts[index],responses[index],advances[index],retirements[index]);
                end
            end
        end
        // Each 16-bit store may merge into one DDR write: 16..32 responses,
        // all of them on the selected phase.
        if(write_responses<WORDS || write_responses>2*WORDS || receipt_phases[response_phase]!=write_responses ||
           race_count!=0 || mismatch_count!=0)begin
            failures=failures+1;$display("RESPONSE_ERROR responses=%0d on_phase=%0d races=%0d mismatches=%0d",
                write_responses,receipt_phases[response_phase],race_count,mismatch_count);
        end
        // Write pauses hit all four lines. Read pauses hit four target
        // read-backs: each line's first read waits for that line's posted
        // writes (kind 2) and then for its line fill (kind 1); in bypass mode
        // kind 1 pauses single-beat reads. Each paused read returns its byte.
        if(pause_assert_phase>=0 && (pause_events!=4 || pause_hold_checks<12 ||
           late_pause_advances!=0 || read_pause_advances!=0 || read_pause_mismatches!=0 || read_pause_mode_errors!=0))begin
            failures=failures+1;$display("PAUSE_ERROR kind=%0d events=%0d native_hold_checks=%0d late_advances=%0d read_advances=%0d read_mismatches=%0d mode_errors=%0d",
                pause_kind,pause_events,pause_hold_checks,late_pause_advances,read_pause_advances,read_pause_mismatches,read_pause_mode_errors);
        end
        $display("HOLD_CHECKS native=%0d pause=%0d pause_events=%0d late_pause_advances=%0d assert_phase=%0d release_phase=%0d kind=%0d",
            native_hold_checks,pause_hold_checks,pause_events,late_pause_advances+read_pause_advances,pause_assert_phase,pause_release_phase,pause_kind);
        $display("SUMMARY phase=%0d delayed_BUFG=%0d prime_cache=%0d DDR_write_responses=%0d phase3_races=%0d data_mismatches=%0d failures=%0d",
            response_phase,$test$plusargs("DELAYED_BUFG"),$test$plusargs("PRIME_CACHE"),write_responses,race_count,mismatch_count,failures);
        $fflush();
        if(failures)$fatal(1,"DDR receipt phase broke CPU completion/retirement contract");
        $display("PASS: posted fast CPU phase=%0d writes/readback, retirement and DDR lane exactly once",response_phase);$finish;
    end
    initial begin #1000000000;$fatal(1,"memory phase timeout");end
endmodule

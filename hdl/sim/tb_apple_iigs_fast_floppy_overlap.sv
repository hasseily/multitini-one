// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// The include is extracted verbatim from the production adapter and machine.
// CPU/cache/native boundaries are fixtures; ownership, CE and cooldown are real.
module tb_apple_iigs_fast_floppy_overlap;
    reg core_clk=0;always #8.730 core_clk=~core_clk;
    wire CPU_CLK=core_clk;
    localparam EXTERNAL_CPU=1;
    reg [1:0] phase=0;
    always @(posedge core_clk)phase<=phase+1'b1;
    wire byte_tick=phase==0,lookup_tick=phase==1,request_tick=phase==2,NATIVE_EDGE=phase==3;
    reg reset=1,machine_reset=1,machine_reset_raw=1,pause_request=0,hdd_dma=0,cache_stall=0;
    reg [127:0] config_bytes=0;
    reg motor35=1,motor525=0;
    reg [1:0] woz_busy=0;
    reg accel_capable=1,eff_accel_en=1,iwm_smartport_mode=0,io_slow_holdoff=0,irq_slowdown=0,twgs_boot_armed=0;
    reg [2:0] eff_speed_code=6,cpu_speed=4;
    reg iwm_native_pending=0;
    reg [14:0] iwm_holdoff=0;
    reg iwm_selected_media=1,floppy_timing_hold=0,iwm_motor_command=1;
    reg [7:0] CYAREG=8'h80,DISK35=8'h40;
    reg valid=1,IO=0;
    reg [15:0] addr_bef=16'h1000;
    reg [23:0] cpu_logical_addr=24'h021000;
    reg [7:0] bus_write_data=8'h69,cpu_floppy_data=8'ha5,native_read_data=8'h7c,bus_read_data=8'h12;
    reg [63:0] bus_read_word=64'h1212121212121212;
    reg bus_write=0,cpu_valid=1,cpu_local_read=1,cpu_fast_cycle=1,l0_hit=0;
    reg cpu_slowram_read=0,cpu_slowram_ready=0,cpu_slowram_pending=0;
    reg [7:0] cpu_slowram_data=8'h12,cpu_local_data=8'h12;
    reg cpu_native_two_edge=0,cpu_native_hold=0,phi2=1,native_return_toggle=0;
    reg native_commit_q;
    reg [63:0] l0_data[0:7];
    wire [2:0] l0_index=cpu_logical_addr[5:3];
    wire fast_floppy_active,floppy_engine_ce,floppy_backend_ce,cpu_floppy_access,cpu_floppy_guard,cpu_floppy_slow;
    wire floppy_write_drain;
    reg floppy_iwm_owned,floppy_iwm_replied;
    reg [7:0] floppy_iwm_addr,floppy_iwm_data,floppy_iwm_response_data;
    reg floppy_iwm_read;
    wire CPU_FLOPPY_PENDING=floppy_iwm_owned;
    wire fast_floppy_stall=floppy_virtual_stall;
    wire flux_pending;
    wire [1:0] floppy_write_pending={1'b0,flux_pending};
    wire [16:0] bit_position;
    wire [5:0] bit_timer;
    `include "apple_iigs_fast_floppy_overlap_production.svh"

    flux_drive drive(
        .IS_35_INCH(1'b1),.DRIVE_ID(2'd0),.CLK_14M(core_clk),.CLOCK_ENABLE(floppy_engine_ce),
        .WRITE_DRAIN_ENABLE(floppy_write_drain),.WRITE_PIPELINE_PENDING(flux_pending),.RESET(reset),
        .PHASES(4'd0),.IMMEDIATE_PHASES(4'd0),.LATCHED_SENSE_REG(3'd0),.IWM_MODE(5'd15),
        .MOTOR_ON(1'b1),.SW_MOTOR_ON(1'b1),.DISKREG_SEL(1'b0),.SEL35(1'b1),.DRIVE_SELECT(1'b0),.DRIVE_SLOT(1'b0),
        .DISK_MOUNTED(1'b1),.DISK_WP(1'b0),.DOUBLE_SIDED(1'b1),.FLUX_TRANSITION(),.WRITE_PROTECT(),.SENSE(),
        .DISK_SWITCHED_OUT(),.STEP_BUSY_OUT(),.STEP_DIR_OUT(),.MOTOR_ON_SENSE_OUT(),.AT_TRACK0_OUT(),
        .MOTOR_SPINNING(),.DRIVE_READY(),.TRACK(),.HEAD_QTRACK(),.EJECT_REQ(),.BIT_POSITION(bit_position),.BIT_TIMER_OUT(bit_timer),
        .TRACK_BIT_COUNT(32'd65536),.TRACK_LOADED(1'b1),.TRACK_LOAD_COMPLETE(1'b0),.BRAM_ADDR(),.BRAM_DATA(8'ha5),
        .IS_FLUX_TRACK(1'b0),.FLUX_DATA_SIZE(32'd0),.FLUX_TOTAL_TICKS(32'd0),.SD_TRACK_REQ(),.SD_TRACK_STROBE(),
        .SD_TRACK_ACK(1'b0),.WRITE_BIT(1'b0),.WRITE_STROBE(1'b0),.WRITE_MODE(1'b0),
        .WRITE_BYTE_OUT(),.WRITE_WE_OUT(),.WRITE_ADDR_OUT(),.CHUNK_RELOAD_REQ(),.CHUNK_NEEDED(),.CHUNK_LOADED(2'd0),.CHUNK_LOADING(1'b0));

    integer case_id=0,ticks=0,retires=0,issues=0,iwm_retires=0,engine_steps=0,backend_steps=0;
    integer last_retire=-1,min_gap=99999,max_gap=0;
    reg [7:0] retired_data=0;
    reg count_gaps=0,check_iwm_tuple=0;
    reg expect_read=1;
    always @(posedge core_clk)begin
        ticks++;
        if(!reset)begin
            if(floppy_engine_ce)engine_steps++;
            if(floppy_backend_ce)backend_steps++;
            if(floppy_iwm_request && floppy_engine_ce)begin
                issues++;
                if(check_iwm_tuple && (floppy_iwm_addr!==8'hec || floppy_iwm_data!==8'h69 || floppy_iwm_read!==expect_read))
                    $fatal(1,"captured IWM bus tuple changed");
                cpu_floppy_data<=8'h5a;
            end
            if(cpu_ce)begin
                retires++;retired_data=cpu_read_data;
                if(floppy_iwm_owned)iwm_retires++;
                if(count_gaps && last_retire>=0)begin
                    if(ticks-last_retire<min_gap)min_gap=ticks-last_retire;
                    if(ticks-last_retire>max_gap)max_gap=ticks-last_retire;
                end
                last_retire=ticks;
            end
        end
    end
    task step(input integer n);repeat(n)@(negedge core_clk);endtask
    task park_check(input integer n);
        integer previous_retires,previous_issues,previous_engine,previous_backend;
        reg [16:0] position;
        reg [5:0] timer;
        begin
            previous_retires=retires;previous_issues=issues;previous_engine=engine_steps;previous_backend=backend_steps;
            position=bit_position;timer=bit_timer;step(n);
            if(retires!=previous_retires || issues!=previous_issues)$fatal(1,"IWM access escaped backend wait");
            if(engine_steps!=previous_engine || bit_position!==position || bit_timer!==timer)$fatal(1,"virtual disk advanced during backend wait");
            if(backend_steps-previous_backend!=n)$fatal(1,"backend controller stopped during wait");
        end
    endtask
    task background(input bit writing,input integer budget,input bit other_drive);
        integer start_retires,start_engine,start_backend;
        reg [16:0] position;
        reg [5:0] timer;
        begin
            bus_write=writing;cpu_local_read=!writing;
            if(budget==5 || budget==14)begin iwm_holdoff=1;CYAREG=budget==14 ? 0:8'h80;end
            woz_busy=other_drive ? 2'b10:2'b01;
            step(4);
            start_retires=retires;start_engine=engine_steps;start_backend=backend_steps;
            position=bit_position;timer=bit_timer;last_retire=-1;min_gap=99999;max_gap=0;count_gaps=1;
            step(280);count_gaps=0;
            if(retires-start_retires<280/budget-1)$fatal(1,"background CPU stalled during backend wait budget=%0d retires=%0d",budget,retires-start_retires);
            if(min_gap!=budget || max_gap!=budget)$fatal(1,"CPU cooldown lost primary budget expected=%0d actual=%0d..%0d",budget,min_gap,max_gap);
            if(engine_steps!=start_engine || bit_position!==position || bit_timer!==timer)$fatal(1,"virtual disk advanced during background work");
            if(backend_steps-start_backend!=280)$fatal(1,"backend FSM lost progress");
            $display("PASS: background CPU write=%0d budget=%0d other_drive=%0d retires=%0d virtual_disk_parked=1",writing,budget,other_drive,retires-start_retires);
        end
    endtask
    task guarded_delay(input integer budget);
        integer before_retires,starting_cooldown,limit;
        begin
            iwm_holdoff=1;CYAREG=budget==14 ? 0:8'h80;before_retires=retires;limit=0;
            while(retires==before_retires && limit<40)begin step(1);limit++;end
            if(limit==40 || cpu_cooldown!=budget-1)$fatal(1,"protected delay did not acquire its budget");
            starting_cooldown=cpu_cooldown;before_retires=retires;woz_busy=1;step(280);
            if(retires!=before_retires || cpu_cooldown!=starting_cooldown)
                $fatal(1,"protected floppy software delay advanced during backend wait");
            woz_busy=0;last_retire=-1;min_gap=99999;max_gap=0;count_gaps=1;step(280);count_gaps=0;
            if(min_gap!=budget || max_gap!=budget)$fatal(1,"protected delay resumed with wrong budget");
            $display("PASS: protected software delay budget=%0d parks and resumes with virtual clock",budget);
        end
    endtask
    task iwm_wait(input bit writing,input bit drop_eligibility);
        integer limit,start_issues,start_retires;
        begin
            woz_busy=1;bus_write=writing;expect_read=!writing;check_iwm_tuple=1;
            IO=1;addr_bef=16'hc0ec;cpu_logical_addr=24'h00c0ec;
            step(3);
            if(!floppy_iwm_owned)$fatal(1,"IWM wait never acquired its captured bus");
            if(drop_eligibility)io_slow_holdoff=1;
            park_check(128);start_issues=issues;start_retires=iwm_retires;
            woz_busy=0;limit=0;
            while(iwm_retires==start_retires && limit<80)begin step(1);limit++;end
            if(limit==80 || issues-start_issues!=1 || iwm_retires-start_retires!=1 || retired_data!==8'ha5)
                $fatal(1,"IWM wait did not complete exactly once with captured response");
            IO=0;addr_bef=16'h1000;cpu_logical_addr=24'h021000;step(2);
            if(floppy_iwm_owned || floppy_iwm_replied || floppy_io_age!=0)$fatal(1,"retired IWM ownership/age did not clear");
            if(drop_eligibility)begin
                repeat(20)begin
                    if(floppy_engine_ce!==NATIVE_EDGE || floppy_backend_ce!==NATIVE_EDGE)$fatal(1,"eligibility loss left virtual clock active");
                    step(1);
                end
            end
            $display("PASS: parked IWM write=%0d eligibility_drop=%0d exact_once=1 cleanup=1",writing,drop_eligibility);
        end
    endtask
    initial begin:test
        integer before_count,start_engine,start_backend,limit;
        if($value$plusargs("CASE=%d",case_id))begin end
        for(integer i=0;i<8;i++)l0_data[i]=0;
        config_bytes[31]=1;step(8);reset=0;step(4);machine_reset=0;machine_reset_raw=0;
        // Mechanical fixture only; the real flux timer/position and all adapter
        // gate, ownership and cooldown registers remain unforced.
        force drive.motor_spinning=1'b1;force drive.drive_ready=1'b1;force drive.prev_drive_ready=1'b1;
        step(256);
        if(!fast_floppy_active || bit_position==0)$fatal(1,"fast/native flux fixture did not run");
        case(case_id)
            0:background(0,4,0);
            1:background(1,4,0);
            2:guarded_delay(5);
            3:guarded_delay(14);
            4:background(0,4,1);
            5:iwm_wait(0,0);
            6:iwm_wait(1,0);
            7:iwm_wait(0,1);
            8:begin
                woz_busy=1;
                for(integer hold=0;hold<5;hold++)begin
                    case(hold)0:cache_stall=1;1:pause_request=1;2:hdd_dma=1;3:machine_reset_raw=1;4:machine_reset=1;endcase
                    before_count=retires;step(32);
                    if(retires!=before_count)$fatal(1,"CPU bypassed independent hold %0d",hold);
                    cache_stall=0;pause_request=0;hdd_dma=0;machine_reset_raw=0;machine_reset=0;step(16);
                end
                $display("PASS: cache pause DMA and both reset CPU holds remain authoritative");
            end
            9:begin
                woz_busy=1;cpu_fast_cycle=0;IO=1;addr_bef=16'hc030;cpu_logical_addr=24'h00c030;
                before_count=retires;start_engine=engine_steps;limit=0;
                while(!native_pending && limit<80)begin step(1);limit++;end
                if(limit==80)$fatal(1,"native bridge stopped while backend waited");
                native_return_toggle=~native_return_toggle;step(1);
                if(retires!=before_count+1 || retired_data!==8'h7c || engine_steps!=start_engine)
                    $fatal(1,"native IO response did not retire with virtual floppy parked");
                $display("PASS: native IO bridge progresses while virtual floppy is parked");
            end
            10:begin
                machine_reset=1;machine_reset_raw=1;config_bytes[31]=0;step(4);machine_reset=0;machine_reset_raw=0;
                woz_busy=3;before_count=retires;start_engine=engine_steps;start_backend=backend_steps;step(280);
                if(fast_floppy_active || retires-before_count<69 || engine_steps-start_engine!=70 || backend_steps-start_backend!=70)
                    $fatal(1,"Normal CPU/native floppy cadence changed");
                $display("PASS: Normal option preserves primary CPU and native floppy/backend cadence");
            end
            11:begin cpu_speed=5;background(0,2,0);end
            12:begin cpu_speed=6;background(1,1,0);end
            13:begin
                // Fixture for the machine's guest-native/active-write guard
                // output, independent of the recent-access holdoff counter.
                iwm_holdoff=0;floppy_timing_hold=1;woz_busy=1;step(1);before_count=cpu_cooldown;
                if(!cpu_floppy_guard || floppy_iwm_owned || cpu_floppy_access)$fatal(1,"timing-hold fixture did not isolate guard reason");
                park_check(128);
                if(cpu_cooldown!=before_count)$fatal(1,"timing-hold guard leaked cooldown progress");
                $display("PASS: machine timing-hold guard parks CPU and real virtual disk with recent-access holdoff zero");
            end
            14:begin
                woz_busy=1;io_slow_holdoff=1;step(8);
                if(floppy_iwm_owned || fast_floppy_active)$fatal(1,"unowned eligibility fixture retained fast access");
                start_engine=engine_steps;start_backend=backend_steps;
                repeat(64)begin
                    if(floppy_engine_ce!==NATIVE_EDGE || floppy_backend_ce!==NATIVE_EDGE)
                        $fatal(1,"unowned eligibility loss did not restore native CE while backend busy");
                    step(1);
                end
                if(engine_steps-start_engine!=16 || backend_steps-start_backend!=16)$fatal(1,"eligibility fallback native cadence changed");
                $display("PASS: unowned eligibility loss restores native floppy/backend CE during backend wait");
            end
            default:$fatal(1,"unknown overlap case");
        endcase
        $finish;
    end
    initial begin #2000000;$fatal(1,"overlap timeout");end
endmodule

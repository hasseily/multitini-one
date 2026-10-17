// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module BUFG(input wire I,output wire O);assign O=I;endmodule
// Exercise the complete production adapter with synthetic ROMs and a real
// 65816 program. No Apple ROM, game or operating-system bytes are included.
module tb_apple_iigs_accel_gates;
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
    reg busy=0,pending_write=0,pending_burst=0;integer beats_left=0;reg [23:0] pending_addr;reg [63:0] pending_data;reg [7:0] pending_mask;
    integer delay_count=0,writes=0,target_lanes=0,target_retired=0;
    assign memory_ready=!busy;
    always @(posedge core_clk)begin
        memory_rvalid<=0;
        if(memory_valid && memory_ready)begin
            pending_addr<=memory_addr;pending_data<=memory_wdata;pending_mask<=memory_wstrb;
            pending_write<=memory_write;delay_count<=3+memory_addr[5:3];busy<=1;
            // Memory interface v2: a burst is a 4-beat read of an aligned line.
            pending_burst<=memory_burst;beats_left<=memory_burst ? 4 : 1;
            if(memory_burst && (memory_write || memory_addr[4:0]!=0))$fatal(1,"invalid DDR burst %x",memory_addr);
        end else if(busy)begin
            if(delay_count!=0)delay_count<=delay_count-1;
            else begin
                if(pending_write)begin
                    for(integer lane=0;lane<8;lane=lane+1)
                        if(pending_mask[lane])memory[pending_addr[23:3]][lane*8+:8]<=pending_data[lane*8+:8];
                    writes<=writes+1;
                    if(pending_addr[23:3]==(24'h002000>>3)) target_lanes<=target_lanes+$countones(pending_mask);
                end
                memory_rdata<=memory[pending_addr[23:3]];memory_rvalid<=1;
                // Ascending beats, sometimes with idle cycles between them.
                if(beats_left>1)begin beats_left<=beats_left-1;pending_addr<=pending_addr+8;delay_count<=pending_addr[3];end
                else busy<=0;
            end
        end
    end
    // Posted writes: count bus write retirements into the target word as
    // the cache enqueues them; DDR lane writes may merge but never exceed them.
    always @(posedge core_clk)if(dut.cache_i.enq && dut.cache_i.enq_addr[23:3]==(24'h002000>>3))target_retired<=target_retired+1;
    task drain;
        integer polls;
        begin
            polls=0;
            while(memory_busy && polls<100000)begin @(negedge core_clk);polls++;end
            if(memory_busy)$fatal(1,"posted DDR writes did not drain");
        end
    endtask
    reg [7:0] rom[0:131071];
    integer speed=6,cursor=0,cases=0,slow_writes=0,native_commits=0,paddle_triggers=0;
    reg [23:0] forced_addr=0;
    reg forced_we_n=1,forced_vda=0,forced_vpa=0,forced_vpb_n=1;
    reg [7:0] forced_data=0;
    longint unsigned primary_ticks=0;
    always @(posedge core_clk) primary_ticks++;
    always @(posedge dut.clk14) if(!dut.machine_reset) begin
        if(dut.machine_i.bus_phi2) native_commits++;
        if(dut.machine_i.paddle_trigger) paddle_triggers++;
    end
    always @(posedge dut.machine_i.slowram.clock_a) if(!dut.machine_reset && dut.machine_i.slowram.enable_a && dut.machine_i.slowram.wren_a)
        slow_writes++;
    // Every port-A write is staged one or two primary edges before it lands.
    // Staging a second write on the phase3 edge that defers the first would
    // drop one, and no staged write may land on the video port's read edge.
    always @(posedge core_clk) begin
        if(dut.machine_i.slowram_wr_set && dut.machine_i.slowram_wr_pending && dut.pixel_ce)
            $fatal(1,"slow-RAM write staged while another was pending: each commit must update the slow-RAM port exactly once");
        if(dut.machine_i.slowram.wren_a && dut.pixel_ce)
            $fatal(1,"slow-RAM port A wrote on the video read edge");
    end
    // HDD DMA pace: primary ticks between consecutive sector bytes reaching
    // the bus. One row per DMA transfer.
    longint unsigned dma_first=0,dma_last=0,dma_min_delta=0,dma_max_delta=0;
    integer dma_bytes=0,dma_rows=0;
    longint unsigned dma_row_ticks[0:7];
    longint unsigned dma_row_min[0:7],dma_row_max[0:7];
    integer dma_row_bytes[0:7];
    always @(posedge dut.clk14) if(!dut.machine_reset) begin
        if(dut.machine_i.hdd.phi0 && dut.machine_i.hdd.dma_state==3) begin
            if(dma_bytes!=0) begin
                if(dma_bytes==1 || primary_ticks-dma_last<dma_min_delta) dma_min_delta=primary_ticks-dma_last;
                if(dma_bytes==1 || primary_ticks-dma_last>dma_max_delta) dma_max_delta=primary_ticks-dma_last;
            end else dma_first=primary_ticks;
            dma_last=primary_ticks;dma_bytes++;
            if(dut.machine_i.hdd.a2_ram_addr==511) begin
                dma_row_ticks[dma_rows&7]=dma_last-dma_first;dma_row_min[dma_rows&7]=dma_min_delta;
                dma_row_max[dma_rows&7]=dma_max_delta;dma_row_bytes[dma_rows&7]=dma_bytes;
                dma_rows++;dma_bytes=0;
            end
        end
    end
    reg [2:0] dbg_dma_state=0;
    always @(posedge dut.clk14) if($test$plusargs("TRACE_DMA") && dut.machine_i.hdd.dma_state!=dbg_dma_state) begin
        dbg_dma_state=dut.machine_i.hdd.dma_state;
        $display("DMA state%0d addr%0d bytes%0d rows%0d tick%0d pc%04x",dbg_dma_state,dut.machine_i.hdd.a2_ram_addr,dma_bytes,dma_rows,primary_ticks,dut.machine_i.cpu.PC);
    end
    task emit(input byte value);begin rom[cursor]=value;cursor++;end endtask
    task packet(input byte kind,input integer size);
        byte value;
        begin
            load_epoch=~load_epoch;
            for(integer i=0;i<size;i++) begin
                value=kind==8'h41 ? rom[i] : kind==8'h43 && i==1 ? speed : kind==8'h45 ? 8'((i*11+5)&255) :
                    kind==8'h48 ? 8'((i*7+19)&255) : 0;
                @(negedge core_clk);load_kind=kind;load_total=size;load_offset=i;load_data=value;
                load_first=i==0;load_last=i==size-1;load_valid=1;
                while(!load_ready) @(negedge core_clk);
                @(negedge core_clk);load_valid=0;
            end
            repeat(40) @(negedge core_clk);
            if(load_error) $fatal(1,"acceleration gate fixture loader error");
        end
    endtask

    reg test_motor525=0,test_motor35=0;
    reg [3:0] test_disks=0;
    // A real IWM latches its mode register on a write to any odd address
    // while Q6=Q7=1 and idle. GS/OS stores mode $04 through $C0ED; with
    // bit 2 set, motor-off must clear the IWM active state at once.
    // The immediate mode view drives the drives and status on the write
    // tick itself; during the motor-on write it must equal the register.
    bit iwm_view_window=0;integer iwm_view_mismatches=0;
    always @(posedge core_clk)
        if(iwm_view_window && dut.machine_i.iwmc.immediate_mode!==dut.machine_i.iwmc.mode_reg) iwm_view_mismatches++;
    task iwm_mode_checks;
        integer polls;
        begin
            side_read(24'h00c0e8);side_read(24'h00c0ed);side_read(24'h00c0ef);
            if(dut.machine_i.iwmc.iwm_active) $fatal(1,"IWM mode fixture: IWM unexpectedly active");
            write_byte(24'h00c0ed,8'h04);
            if(dut.machine_i.iwmc.mode_reg!==8'h04) $fatal(1,"IWM odd-address mode write ignored mode%02x",dut.machine_i.iwmc.mode_reg);
            side_read(24'h00c0e9);
            if(!dut.machine_i.iwmc.iwm_active) $fatal(1,"IWM motor-on did not activate the IWM");
            side_read(24'h00c0e8);
            polls=0;
            while(dut.machine_i.iwmc.iwm_active && polls<400) begin @(negedge core_clk);polls++;end
            if(dut.machine_i.iwmc.iwm_active) $fatal(1,"IWM odd-address mode write did not make motor-off immediate");
            // A write to $C0E9 (motor on) from idle with Q6=Q7=1 is a motor and
            // data access, as in MAME: the mode register keeps its value.
            side_read(24'h00c0ed);side_read(24'h00c0ef);
            if(dut.machine_i.iwmc.iwm_active) $fatal(1,"IWM motor-on fixture: IWM unexpectedly active");
            iwm_view_mismatches=0;iwm_view_window=1;
            write_byte(24'h00c0e9,8'h1b);
            iwm_view_window=0;
            if(dut.machine_i.iwmc.mode_reg!==8'h04) $fatal(1,"IWM motor-on write changed the mode register mode%02x",dut.machine_i.iwmc.mode_reg);
            if(iwm_view_mismatches!=0) $fatal(1,"IWM motor-on write changed the immediate mode view on %0d ticks",iwm_view_mismatches);
            if(!dut.machine_i.iwmc.iwm_active || !dut.machine_i.iwmc.drive_on) $fatal(1,"IWM motor-on write did not start the motor");
            side_read(24'h00c0ee);side_read(24'h00c0e8);
            polls=0;
            while(dut.machine_i.iwmc.iwm_active && polls<400) begin @(negedge core_clk);polls++;end
            if(dut.machine_i.iwmc.iwm_active) $fatal(1,"IWM motor-off after motor-on write was not immediate");
            // Restore mode $00 through the classic $C0EF store.
            side_read(24'h00c0ed);side_read(24'h00c0ef);write_byte(24'h00c0ef,8'h00);
            if(dut.machine_i.iwmc.mode_reg!==8'h00) $fatal(1,"IWM $C0EF mode write ignored mode%02x",dut.machine_i.iwmc.mode_reg);
            side_read(24'h00c0ec);
            mark("IWM mode register latches on odd addresses except motor-on with Q6=Q7=1 while idle; mode bit 2 makes motor-off immediate");
        end
    endtask
    task motor_checks;
        bit expected_native;
        integer motor_checks_count;
        begin
            // A real empty-drive motor command still reaches the controller
            // and arms the IWM access timer, but with no media in the selected
            // drive family the two-ms access hold leaves the accelerator on.
            side_read(24'h00c0e9);
            if(dut.machine_i.iwm_holdoff==0 || !dut.machine_i.iwmc.drive_on)
                $fatal(1,"empty-drive IWM access lost native command");
            if(dut.cpu_speed!=6) $fatal(1,"empty-drive IWM access held the accelerator");
            force dut.machine_i.iwmc.motor_spinning=test_motor525;
            force dut.machine_i.iwmc.drive35_motor_spinning=test_motor35;
            force dut.machine_i.DISK_READY=test_disks;
            // Motors forced off: only the access hold can clamp the speed.
            test_disks=4'b0001;#1; // 5.25-inch media, 5.25-inch family selected.
            if(dut.cpu_speed!=0) $fatal(1,"mounted 5.25 IWM access lost two-ms hold");
            // The hold follows the selected drive, not its family: probes of
            // the empty drive 2 stay accelerated while drive 1 holds a disk.
            side_read(24'h00c0eb);
            if(dut.machine_i.iwm_holdoff==0 || dut.cpu_speed!=6) $fatal(1,"empty selected 5.25 drive 2 kept IWM hold");
            side_read(24'h00c0ea);
            if(dut.cpu_speed!=0) $fatal(1,"mounted selected 5.25 drive 1 lost IWM hold");
            write_byte(24'h00c031,8'h40); // Select the 3.5-inch family (empty).
            if(dut.machine_i.iwm_holdoff==0 || dut.cpu_speed!=6) $fatal(1,"empty selected 3.5 family kept IWM hold");
            test_disks=4'b0100;#1;
            if(dut.cpu_speed!=0) $fatal(1,"mounted 3.5 IWM access lost two-ms hold");
            side_read(24'h00c0eb);
            if(dut.machine_i.iwm_holdoff==0 || dut.cpu_speed!=6) $fatal(1,"empty selected 3.5 drive 2 kept IWM hold");
            side_read(24'h00c0ea);
            if(dut.cpu_speed!=0) $fatal(1,"mounted selected 3.5 drive 1 lost IWM hold");
            write_byte(24'h00c031,8'h00);
            if(dut.cpu_speed!=6) $fatal(1,"empty selected 5.25 family kept IWM hold");
            test_disks=4'b0001;side_read(24'h00c0ec);
            if(dut.machine_i.iwm_holdoff==0 || dut.cpu_speed!=0) $fatal(1,"mounted IWM access did not rearm hold");
            repeat(4*28636+8) @(negedge core_clk);
            if(dut.machine_i.iwm_holdoff!=0 || dut.cpu_speed!=6) $fatal(1,"IWM access delay failed to expire");
            test_motor525=1;test_motor35=0;test_disks=0;#1;
            if(dut.cpu_speed!=6) $fatal(1,"unmounted IWM motor latch falsely clamps accelerator");
            if(!dut.machine_i.floppy_motor_on || !dut.machine_i.iwmc.drive_on)
                $fatal(1,"accelerator gating changed raw motor output or command");
            motor_checks_count=0;
            for(integer disks=0;disks<16;disks++) for(integer motors=0;motors<4;motors++) begin
                test_disks=disks;test_motor525=motors[0];test_motor35=motors[1];#0.001;
                expected_native=((disks&3)!=0 && motors[0]) || ((disks&12)!=0 && motors[1]);
                if(dut.cpu_speed!==(expected_native ? 3'd0:3'd6))
                    $fatal(1,"mounted motor family timing failed disks%0h motors%0d speed%0d",test_disks,motors,dut.cpu_speed);
                if(dut.machine_i.floppy_motor_on!==test_motor525 || dut.machine_i.floppy35_motor_on!==test_motor35)
                    $fatal(1,"raw motor status changed with accelerator gate");
                motor_checks_count++;
            end
            test_disks=1;test_motor525=1;test_motor35=0;
            memory[24'h001008>>3]=64'h3c3c3c3c3c3c3c3c;
            read_byte(24'h001008,8'h3c,1); // Mounted motor retains native bus cadence.
            test_disks=0;read_byte(24'h001008,8'h3c,0); // Eject releases only the accelerator.
            release dut.machine_i.iwmc.motor_spinning;
            release dut.machine_i.iwmc.drive35_motor_spinning;
            release dut.machine_i.DISK_READY;
            $display("COMPARE mounted_motor=%0d",motor_checks_count);
            mark("empty-drive motor and IWM access keep acceleration; mounted-family motor and selected-drive IWM holds retained");
        end
    endtask
    bit mount_disk=0;
    task start_machine;
        begin
            reset=1;core_reset_request=1;
            repeat(20) @(negedge core_clk);reset=0;
            packet(8'h43,16);packet(8'h41,131072);packet(8'h42,4096);packet(8'h46,4096);packet(8'h45,256);
            if(!rom_loaded) $fatal(1,"synthetic ROM fixtures not loaded");
            if(mount_disk) begin
                media_slot=0;media_kind=8'h40;media_image_size=1024;media_generation=32'h5a;media_read_only=0;
                media_mount=1;@(negedge core_clk);media_mount=0;repeat(100) @(negedge core_clk);
            end
            core_reset_request=0;
            while(dut.machine_reset || dut.machine_reset_raw) @(negedge core_clk);
        end
    endtask
    byte bus_received;
    task bus_cycle(input [23:0] address,input bit wr,input byte data,input bit check,input byte expected,input bit expected_native);
        integer polls,commits_before,slow_before,ddr_before;
        byte received;
        begin
            @(negedge core_clk);pause_request=1;
            forced_addr=address;forced_we_n=!wr;forced_data=data;forced_vda=1;forced_vpa=0;
            commits_before=native_commits;slow_before=slow_writes;ddr_before=writes;
            @(negedge core_clk);pause_request=0;polls=0;
            begin:wait_response
                while(polls<200000) begin
                    @(posedge core_clk);
                    if(dut.cpu_ce) begin
                        received=dut.cpu_read_data;
                        if(dut.native_pending!==expected_native) $fatal(1,"wrong bridge class address%06x native%0b expected%0b",address,dut.native_pending,expected_native);
                        disable wait_response;
                    end
                    polls++;
                end
                $fatal(1,"acceleration gate transaction timeout address%06x",address);
            end
            @(negedge core_clk);pause_request=1;forced_vda=0;bus_received=received;
            if(check && received!==expected) $fatal(1,"gate read data address%06x got%02x expected%02x",address,received,expected);
            if(native_commits-commits_before!=integer'(expected_native)) $fatal(1,"native completion count address%06x count%0d",address,native_commits-commits_before);
            // A retired write is posted; once drained it reached DDR once.
            // Its slow-RAM copy lands within two primary edges of retirement.
            if(wr)begin repeat(3) @(negedge core_clk);drain();end
            if(wr && address==24'h002000 && writes-ddr_before!=1)
                $fatal(1,"shadow write must update DDR exactly once after drain count%0d",writes-ddr_before);
            if(wr && dut.machine_i.slowram_ce && dut.machine_i.slowram_we && slow_writes-slow_before!=1)
                $fatal(1,"slow-RAM write address%06x must update the slow-RAM port exactly once count%0d",address,slow_writes-slow_before);
        end
    endtask
    task read_byte(input [23:0] address,input byte value,input bit native_cycle);
        bus_cycle(address,0,0,1,value,native_cycle);
    endtask
    task side_read(input [23:0] address);
        bus_cycle(address,0,0,0,0,1);
    endtask
    task write_byte(input [23:0] address,input byte value);
        bus_cycle(address,1,value,0,0,1);
    endtask
    task write_ram(input [23:0] address,input byte value,input bit native_cycle);
        bus_cycle(address,1,value,0,0,native_cycle);
    endtask
    task mark(input string name);begin cases++;$display("PASS: acceleration gates %s",name);end endtask

    task joystick_packet(input [15:0] xy);
        begin
            @(negedge core_clk);input_code=16'h4101;input_value=xy;input_valid=1;
            do @(posedge core_clk); while(!input_ready);
            @(negedge core_clk);input_valid=0;
        end
    endtask
    task joystick_axes(input [15:0] xy,input string what);
        begin
            if(dut.machine_i.paddle_input[0]!==xy[7:0] || dut.machine_i.paddle_input[1]!==xy[15:8])
                $fatal(1,"joystick %s got X=%02x Y=%02x expected %04x",what,
                    dut.machine_i.paddle_input[0],dut.machine_i.paddle_input[1],xy);
            if(dut.machine_i.paddle_input[2]!==8'h80 || dut.machine_i.paddle_input[3]!==8'h80)
                $fatal(1,"unassigned second joystick must stay centered");
        end
    endtask
    task paddle_bit(input integer axis,input bit active);
        begin
            side_read(24'h00c064+24'(axis));
            if(bus_received[7]!==active)
                $fatal(1,"paddle C06%0d bit7=%0b expected%0b age=%0d",axis+4,bus_received[7],active,
                    dut.machine_i.tick_counter_14m-dut.machine_i.paddle_timers[0].timer_inst.trigger_tick);
        end
    endtask
    task paddle_age(input integer ticks);
        begin
            while((dut.machine_i.tick_counter_14m-dut.machine_i.paddle_timers[0].timer_inst.trigger_tick)<32'(ticks))
                @(negedge core_clk);
        end
    endtask
    task paddle_start(input bit write_trigger);
        integer triggers_before,polls;
        begin
            triggers_before=paddle_triggers;
            if(write_trigger)write_byte(24'h00c070,0);
            else side_read(24'h00c070);
            // CPU retirement precedes the registered trigger's consumption
            // by the timers. Start age checks only after that native edge.
            polls=0;
            while(paddle_triggers==triggers_before && polls<64) begin @(negedge core_clk);polls++;end
            if(paddle_triggers!=triggers_before+1) $fatal(1,"C070 did not trigger the paddle timers exactly once");
        end
    endtask
    task joystick_checks;
        begin
            // Production transport is unsigned, while the imported machine
            // accepts signed MiSTer axes. Include reset before any packets.
            joystick_axes(16'h8080,"neutral after reset");
            for(integer axis=0;axis<256;axis++) begin
                joystick_packet({8'h80,8'(axis)});joystick_axes({8'h80,8'(axis)},"X range");
                joystick_packet({8'(axis),8'h80});joystick_axes({8'(axis),8'h80},"Y range");
            end
            joystick_packet(16'h0000);joystick_axes(16'h0000,"top left");
            joystick_packet(16'hffff);joystick_axes(16'hffff,"bottom right");
            joystick_packet(16'h8080);joystick_axes(16'h8080,"release/disconnect neutral");
            // Read the actual Apple-visible bit, allowing the native bus
            // transaction margin on either side of each timer deadline.
            paddle_start(0);
            paddle_bit(0,1);paddle_bit(1,1);
            paddle_age(128*158-64);paddle_bit(0,1);paddle_bit(1,1);
            paddle_age(128*158+4);paddle_bit(0,0);paddle_bit(1,0);
            joystick_packet(16'hff00);joystick_axes(16'hff00,"X minimum Y maximum");
            paddle_start(1);
            paddle_age(64);paddle_bit(0,0);paddle_bit(1,1);
            paddle_age(255*158-64);paddle_bit(0,0);paddle_bit(1,1);
            paddle_age(255*158+4);paddle_bit(0,0);paddle_bit(1,0);
            joystick_packet(16'h00ff);joystick_axes(16'h00ff,"X maximum Y minimum");
            paddle_start(0);
            paddle_age(64);paddle_bit(0,1);paddle_bit(1,0);
            paddle_age(255*158-64);paddle_bit(0,1);paddle_bit(1,0);
            paddle_age(255*158+4);paddle_bit(0,0);paddle_bit(1,0);
            joystick_packet(16'h8080);joystick_axes(16'h8080,"neutral after travel");
            // Leave the following compatibility checks at maximum speed.
            repeat(4*57272+8) @(negedge core_clk);
            mark("unsigned joystick full X/Y ranges, reset/release center and C070-triggered C064/C065 timing at center and independent endpoints");
        end
    endtask

    task paddle_checks;
        integer trigger_start;
        reg [23:0] bank_base[0:4];
        begin
            bank_base[0]=24'h000000;bank_base[1]=24'h010000;
            bank_base[2]=24'he00000;bank_base[3]=24'he10000;bank_base[4]=24'h020000;
            write_byte(24'h00c036,8'h90); // Enable modeled all-bank IO aliases.
            trigger_start=paddle_triggers;
            // IRQ-dispatch ROM and unused neighboring writes are not the IIgs
            // paddle-reset register. Exercise every decoded bank alias.
            for(integer bank=0;bank<5;bank++) for(integer selector=1;selector<16;selector++) begin
                side_read(bank_base[bank]+24'h00c070+selector);
                if(dut.machine_i.pdl_holdoff!=0) $fatal(1,"IRQ dispatch falsely arms paddle holdoff address%06x",bank_base[bank]+24'h00c070+selector);
                write_byte(bank_base[bank]+24'h00c070+selector,0);
                if(dut.machine_i.pdl_holdoff!=0) $fatal(1,"non-paddle C07x write arms holdoff");
            end
            if(paddle_triggers!=trigger_start) $fatal(1,"IRQ dispatch triggered physical paddle timers");
            mark("all C071-C07F read/write bank aliases leave paddle holdoff idle");
            for(integer bank=0;bank<5;bank++) begin
                side_read(bank_base[bank]+24'h00c070);
                if(dut.machine_i.pdl_holdoff==0 || dut.cpu_speed!=0) $fatal(1,"C070 read must retain native paddle delay");
                write_byte(bank_base[bank]+24'h00c070,0);
                if(dut.machine_i.pdl_holdoff==0) $fatal(1,"C070 write lost paddle delay");
            end
            for(integer selector=0;selector<4;selector++) begin
                side_read(24'h00c064+selector);
                if(dut.machine_i.pdl_holdoff==0) $fatal(1,"paddle timer read lost compatibility delay");
            end
            // Four native milliseconds must elapse before acceleration returns.
            repeat(4*57272+8) @(negedge core_clk);
            if(dut.machine_i.pdl_holdoff!=0 || dut.cpu_speed!=6) $fatal(1,"paddle compatibility delay failed to expire");
            mark("C070 read/write and C064-C067 retain four-ms delay then restore max");
            write_byte(24'h00c035,8'h7f); // C070 becomes RAM in bank00/01.
            memory[24'h00c070>>3]=64'h3333333333333333;
            read_byte(24'h00c070,8'h33,0);
            if(dut.machine_i.pdl_holdoff!=0) $fatal(1,"IOLC-inhibited RAM alias triggered paddle delay");
            write_byte(24'he0c035,0);write_byte(24'h00c036,8'h80);
            mark("IOLC-inhibited C070 RAM never triggers IO paddle delay");
        end
    endtask

    reg [7:0] test_shadow=0,test_cya=8'h80;
    reg test_rom=0;
    integer decode_checks=0;
    task shadow_checks;
        reg [15:0] addresses[0:11];
        bit expected_fast;
        begin
            pause_request=1;forced_vda=1;forced_we_n=1;forced_addr=24'h002000;
            #1;
            if(!dut.machine_i.fastram_ce || !dut.machine_i.slowram_ce || !dut.cpu_fast_cycle)
                $fatal(1,"fast shadow RAM read incorrectly classified native");
            addresses[0]=16'h0000;addresses[1]=16'h0400;addresses[2]=16'h07ff;
            addresses[3]=16'h0800;addresses[4]=16'h0bff;addresses[5]=16'h1fff;
            addresses[6]=16'h2000;addresses[7]=16'h3fff;addresses[8]=16'h4000;
            addresses[9]=16'h5fff;addresses[10]=16'h6000;addresses[11]=16'h9fff;
            force dut.machine_i.rom_select=test_rom;
            // Independent expectation for ordinary populated RAM: reads and
            // writes are fast in every shadow mode; a write uses the
            // primary-clock slow-RAM port exactly when the mirror is enabled.
            for(integer rom_mode=0;rom_mode<2;rom_mode++) for(integer shadow=0;shadow<128;shadow++)
                for(integer allbank=0;allbank<2;allbank++) begin
                    test_shadow=shadow;test_rom=rom_mode;test_cya=allbank ? 8'h90:8'h80;
                    write_byte(24'he0c035,test_shadow);write_byte(24'he0c036,test_cya);
                    forced_vda=1;
                    for(integer bank=0;bank<4;bank++)
                    for(integer address=0;address<12;address++) for(integer wr=0;wr<2;wr++) begin
                        forced_addr={8'(bank),addresses[address]};forced_we_n=!wr;#0.001;
                        expected_fast=wr && dut.machine_i.slowram_ce;
                        if(!dut.machine_i.fastram_ce || dut.cpu_fast_cycle!==1'b1 || dut.cpu_slowram_write!==expected_fast ||
                           dut.cpu_slowram_read!==1'b0)
                            $fatal(1,"RAM shadow class bank%0d addr%04x shadow%02x ROM%0b allbank%0b wr%0b fast%0b mirror%0b",bank,addresses[address],
                                test_shadow,test_rom,allbank,wr,dut.cpu_fast_cycle,dut.cpu_slowram_write);
                        decode_checks++;
                    end
                end
            // Direct Mega II RAM reads use the slow-RAM port and LC ROM reads
            // the ROM cache; $C000-$CFFF (IO, slots, C800) stays native.
            for(integer bank=224;bank<=225;bank++) for(integer addr=0;addr<65536;addr+=256) begin
                forced_addr={8'(bank),16'(addr)};forced_we_n=1;#0.001;
                expected_fast=addr<'hc000 || addr>='hd000;
                if(dut.cpu_fast_cycle!==expected_fast || dut.cpu_slowram_read!==(expected_fast && !dut.machine_i.rom_ce) ||
                   dut.cpu_slowram_write!==1'b0)
                    $fatal(1,"direct Mega II class bank%02x addr%04x fast%0b slowram%0b",bank,addr,dut.cpu_fast_cycle,dut.cpu_slowram_read);
                decode_checks++;
            end
            release dut.machine_i.rom_select;
            forced_vda=0;
            $display("COMPARE shadow_ram=%0d",decode_checks);
            mark("all shadow/config RAM reads and writes fast; E0/E1 RAM fast and E0/E1 C000-CFFF native");
            // Real production bridge writes must preserve both memories, and
            // their later fast vs slow reads must return their own exact copy.
            write_byte(24'he0c035,0);write_byte(24'he0c036,8'h80);
            write_ram(24'h002000,8'h5a,0);drain();
            if(memory[24'h002000>>3][7:0]!==8'h5a) $fatal(1,"shadow write lost main DDR copy");
            read_byte(24'h002000,8'h5a,0);read_byte(24'he02000,8'h5a,0);
            write_ram(24'he02000,8'ha6,0);
            read_byte(24'h002000,8'h5a,0);read_byte(24'he02000,8'ha6,0);
            write_ram(24'h016000,8'hb7,0);
            read_byte(24'h016000,8'hb7,0);read_byte(24'he16000,8'hb7,0);
            // Stock speed keeps every Mega II access on the native bridge.
            speed=0;packet(8'h43,16);
            if(dut.cpu_speed!=0) $fatal(1,"stock configuration did not apply");
            read_byte(24'he02000,8'ha6,1);write_ram(24'he02000,8'hc4,1);read_byte(24'he02000,8'hc4,1);
            write_ram(24'h002000,8'hd5,1);read_byte(24'he02000,8'hd5,1);read_byte(24'h002000,8'hd5,1);
            speed=6;packet(8'h43,16);
            if(dut.cpu_speed!=6) $fatal(1,"maximum configuration did not apply");
            read_byte(24'he02000,8'hd5,0);
            mark("fast and stock shadow and Mega II writes retire once and preserve DDR/slow-RAM mirrors");
        end
    endtask

    // Physical slow-RAM byte, as both CPU port A and video port B address it.
    function automatic [7:0] slow_byte(input [16:0] a);
        case(a[16:14])
            0:slow_byte=dut.machine_i.slowram.g_bank[0].ram.mem[a[13:0]];
            1:slow_byte=dut.machine_i.slowram.g_bank[1].ram.mem[a[13:0]];
            2:slow_byte=dut.machine_i.slowram.g_bank[2].ram.mem[a[13:0]];
            3:slow_byte=dut.machine_i.slowram.g_bank[3].ram.mem[a[13:0]];
            4:slow_byte=dut.machine_i.slowram.g_bank[4].ram.mem[a[13:0]];
            5:slow_byte=dut.machine_i.slowram.g_bank[5].ram.mem[a[13:0]];
            6:slow_byte=dut.machine_i.slowram.g_bank[6].ram.mem[a[13:0]];
            default:slow_byte=dut.machine_i.slowram.g_bank[7].ram.mem[a[13:0]];
        endcase
    endfunction
    task expect_slow(input [16:0] a,input byte value,input string what);
        begin
            if(slow_byte(a)!==value) $fatal(1,"%s slow RAM %05x got %02x expected %02x",what,a,slow_byte(a),value);
            // The video port reads the same byte on its own phase3 enable.
            force dut.machine_i.video_addr=23'(a);
            repeat(12) @(negedge core_clk);
            if(dut.machine_i.video_data!==value) $fatal(1,"%s video port %05x got %02x expected %02x",what,a,dut.machine_i.video_data,value);
            release dut.machine_i.video_addr;
        end
    endtask
    task mega2_mapping_checks;
        begin
            if(dut.cpu_speed!=6) $fatal(1,"Mega II mapping checks require maximum speed");
            // Bank latch on (NEWVIDEO[0]=1 after reset): E0 and E1 are distinct.
            write_ram(24'he03000,8'h11,0);write_ram(24'he13000,8'h22,0);
            read_byte(24'he03000,8'h11,0);read_byte(24'he13000,8'h22,0);
            expect_slow(17'h03000,8'h11,"E0 bank latch");expect_slow(17'h13000,8'h22,"E1 bank latch");
            write_byte(24'he0c029,8'h40); // Bank latch off: E1 behaves as E0.
            read_byte(24'he13000,8'h11,0);write_ram(24'he13000,8'h33,0);read_byte(24'he03000,8'h33,0);
            expect_slow(17'h03000,8'h33,"E1-as-E0");expect_slow(17'h13000,8'h22,"latched E1 untouched");
            write_byte(24'he0c029,8'h41);
            read_byte(24'he13000,8'h22,0);
            // ALTZP maps E0 page 01 to E1.
            write_ram(24'he00100,8'h55,0);
            write_byte(24'he0c009,0);write_ram(24'he00100,8'h44,0);read_byte(24'he00100,8'h44,0);
            write_byte(24'he0c008,0);read_byte(24'he00100,8'h55,0);read_byte(24'he10100,8'h44,0);
            expect_slow(17'h00100,8'h55,"ALTZP off");expect_slow(17'h10100,8'h44,"ALTZP on");
            // RAMWRT/RAMRD map ordinary E0 pages to E1 per direction.
            write_ram(24'he01000,8'h77,0);
            write_byte(24'he0c005,0);write_ram(24'he01000,8'h66,0);read_byte(24'he01000,8'h77,0);
            write_byte(24'he0c003,0);read_byte(24'he01000,8'h66,0);
            write_byte(24'he0c002,0);write_byte(24'he0c004,0);read_byte(24'he01000,8'h77,0);
            expect_slow(17'h01000,8'h77,"RAMWRT main");expect_slow(17'h11000,8'h66,"RAMWRT aux");
            // Language card: bank1 folds to physical C000, bank2 stays D000,
            // write protect discards, and ROM reads use the ROM cache.
            side_read(24'he0c08b);side_read(24'he0c08b); // Bank1 RAM read/write.
            write_ram(24'he0d000,8'h88,0);read_byte(24'he0d000,8'h88,0);
            side_read(24'he0c083);side_read(24'he0c083); // Bank2 RAM read/write.
            write_ram(24'he0d000,8'h99,0);read_byte(24'he0d000,8'h99,0);
            write_ram(24'he0e000,8'hab,0);read_byte(24'he0e000,8'hab,0);
            side_read(24'he0c08b);read_byte(24'he0d000,8'h88,0);
            expect_slow(17'h0c000,8'h88,"LC bank1");expect_slow(17'h0d000,8'h99,"LC bank2");
            side_read(24'he0c080); // Bank2 RAM read, write protected.
            write_ram(24'he0d000,8'hcc,0);read_byte(24'he0d000,8'h99,0);
            expect_slow(17'h0d000,8'h99,"LC write protect");
            side_read(24'he0c082); // ROM read, write protected.
            read_byte(24'he0e000,8'hea,0);read_byte(24'he0d000,8'hea,0);
            mark("Mega II fast reads/writes honor bank latch, ALTZP, RAMRD/RAMWRT and LC banks; video port coherent");
        end
    endtask
    longint unsigned loop_start=0,loop_advances=0,loop_elapsed=0,loop_slow_reads=0;
    bit measuring=0,write_kernel=0;
    reg [15:0] kernel_start=16'h2000,kernel_end=16'h2004,kernel_warm_x=16'h0f00;
    always @(posedge core_clk) if(!dut.machine_reset && dut.cpu_ce && !pause_request && !forced_vda) begin
        if(dut.machine_i.cpu.STATE==0 && dut.machine_i.cpu.PBR==0 && dut.machine_i.cpu.PC==kernel_start && dut.machine_i.cpu.X==kernel_warm_x) begin
            measuring=1;loop_start=primary_ticks;loop_advances=0;loop_slow_reads=0;
        end
        if(measuring) begin
            if(dut.machine_i.pdl_holdoff!=0) $fatal(1,"real IRQ dispatch ROM fetch armed paddle delay");
            if(dut.machine_i.cpu.STATE==0 && dut.machine_i.cpu.PC==kernel_end) begin
                measuring=0;loop_elapsed=primary_ticks-loop_start;
            end else begin
                loop_advances++;
                if(dut.cpu_slowram_ready && !dut.native_pending) loop_slow_reads++;
            end
        end
    end
    // The video timebase free-runs through machine reset and the stock PH0
    // grid re-anchors to its line, so stock cadence depends on where in the
    // 912-tick line the CPU starts. Start every kernel at the same beam
    // position so stock measurements do not depend on earlier bench history.
    task beam_sync;
        integer polls;
        begin
            polls=0;
            while(!(dut.machine_i.H==0 && dut.phase==0) && polls<8000) begin @(negedge core_clk);polls++;end
            if(polls>=8000) $fatal(1,"beam position sync timeout");
        end
    endtask
    task real_cpu_kernel;
        integer polls;
        begin
            pause_request=1;forced_vda=0;
            release dut.machine_i.cpu_addr;release dut.machine_i.cpu_vpa;release dut.machine_i.cpu_vda;
            release dut.machine_i.cpu_we_n;release dut.machine_i.cpu_dout;release dut.machine_i.cpu_vpb;
            release dut.machine_i.cpu_i_flag;
            for(integer setting=0;setting<2;setting++) begin
                speed=setting==0 ? 0:6;loop_elapsed=0;measuring=0;
                for(integer i=0;i<131072;i++) rom[i]=8'hea;
                rom[131068]=0;rom[131069]=8'he0;
                rom[65536+16'hc074]=8'h5c;rom[65536+16'hc075]=0;
                rom[65536+16'hc076]=8'h20;rom[65536+16'hc077]=0;
                cursor=122880;emit(8'h78);emit(8'h18);emit(8'hfb);emit(8'hc2);emit(8'h30);
                emit(8'ha9);emit(8'h34);emit(8'h12);emit(8'ha2);emit(0);emit(8'h10);emit(8'h58);
                emit(8'h5c);emit(8'h74);emit(8'hc0);emit(0);
                // INC A; DEX; BNE $2000; STA $020100; STP.
                memory[24'h002000>>3]=64'h0201008ffcd0ca1a;
                memory[(24'h002000>>3)+1]=64'heaeaeaeaeaeaeAdb;
                start_machine();beam_sync();pause_request=0;polls=0;
                while(!dut.machine_i.cpu.STPExec && polls<2000000) begin @(negedge core_clk);polls++;end
                pause_request=1;drain();
                if(!dut.machine_i.cpu.STPExec || loop_elapsed==0 || dut.machine_i.cpu.A!==16'h2234 || dut.machine_i.cpu.X!==0 ||
                   memory[24'h020100>>3][15:0]!==16'h2234) $fatal(1,"real shadow-RAM kernel architectural state incorrect");
                if(speed==6 && loop_elapsed!=loop_advances) $fatal(1,"warm shadow RAM kernel did not reach one primary tick per CPU advance ticks%0d advances%0d",loop_elapsed,loop_advances);
                $display("MEASURE shadow_kernel speed=%0d primary_ticks=%0d cpu_advances=%0d",speed,loop_elapsed,loop_advances);
            end
            mark("real 65816 IRQ-ROM-to-shadow-RAM loop exact stock/max execution and 57MHz warm cadence");
        end
    endtask
    // Real 65816 store loops. Bank 00 $2000 is a shadowed store (fast RAM
    // plus the Mega II mirror at E0:2000); bank E1 $2000 is a direct Mega II
    // store. DDR contents are checked only after a drain pause.
    task store_kernel(input string name,input [7:0] target_bank);
        integer polls,slow_before,ddr_before,retired_before;
        bit mirror_ok;
        begin
            write_kernel=1;kernel_start=16'he080;kernel_end=16'he088;kernel_warm_x=16'h01f0;
            for(integer setting=0;setting<2;setting++) begin
                speed=setting==0 ? 0:6;loop_elapsed=0;measuring=0;
                for(integer i=0;i<131072;i++) rom[i]=8'hea;
                rom[131068]=0;rom[131069]=8'he0;
                cursor=122880;emit(8'h78);emit(8'h18);emit(8'hfb);emit(8'hc2);emit(8'h30);
                emit(8'ha9);emit(8'h34);emit(8'h12);emit(8'ha2);emit(0);emit(2);emit(8'h58);
                emit(8'h4c);emit(8'h80);emit(8'he0);
                cursor=122880+128;
                emit(8'h1a); // INC A.
                emit(8'h8f);emit(0);emit(8'h20);emit(target_bank); // STA long $xx2000.
                emit(8'hca);emit(8'hd0);emit(8'hf8);emit(8'hdb);
                start_machine();beam_sync();slow_before=slow_writes;ddr_before=target_lanes;retired_before=target_retired;pause_request=0;polls=0;
                while(!dut.machine_i.cpu.STPExec && polls<2000000) begin @(negedge core_clk);polls++;end
                pause_request=1;drain();
                mirror_ok=target_bank==8'he1 ?
                    (dut.machine_i.slowram.g_bank[4].ram.mem[8192]===8'h34 && dut.machine_i.slowram.g_bank[4].ram.mem[8193]===8'h14) :
                    (dut.machine_i.slowram.g_bank[0].ram.mem[8192]===8'h34 && dut.machine_i.slowram.g_bank[0].ram.mem[8193]===8'h14 &&
                     memory[24'h002000>>3][15:0]===16'h1434);
                if(!dut.machine_i.cpu.STPExec || loop_elapsed==0 || dut.machine_i.cpu.A!==16'h1434 || dut.machine_i.cpu.X!==0 ||
                   !mirror_ok || slow_writes-slow_before!=1024 ||
                   (target_bank==8'h00 ? (target_retired-retired_before!=1024 || target_lanes-ddr_before>1024 || target_lanes-ddr_before<2) :
                                         (target_retired-retired_before!=0 || target_lanes-ddr_before!=0)))
                    $fatal(1,"%s lost data or single write retirement slow%0d retired%0d DDR lanes%0d",name,
                        slow_writes-slow_before,target_retired-retired_before,target_lanes-ddr_before);
                $display("MEASURE %s speed=%0d primary_ticks=%0d cpu_advances=%0d stores=1024 ddr_lanes=%0d",name,speed,loop_elapsed,loop_advances,
                    target_lanes-ddr_before);
            end
            write_kernel=0;
        end
    endtask
    task native_write_kernel;
        begin
            store_kernel("shadow_store_kernel",8'h00);
            mark("real 65816 shadow-store workload preserves 1024 exact mirror writes per rate");
            store_kernel("mega2_store_kernel",8'he1);
            mark("real 65816 bank-E1 store workload preserves 1024 exact writes per rate");
        end
    endtask
    // Real 65816 bank-E1 load loop: LDA long $E12000 (16-bit A, two Mega II
    // reads); DEX; BNE. Each primary-clock E0/E1 read completes from its
    // registered data three primary ticks after its bus appears.
    task load_kernel;
        integer polls;
        begin
            write_kernel=0;kernel_start=16'he080;kernel_end=16'he087;kernel_warm_x=16'h01f0;
            for(integer setting=0;setting<2;setting++) begin
                speed=setting==0 ? 0:6;loop_elapsed=0;measuring=0;
                for(integer i=0;i<131072;i++) rom[i]=8'hea;
                rom[131068]=0;rom[131069]=8'he0;
                cursor=122880;emit(8'h78);emit(8'h18);emit(8'hfb);emit(8'hc2);emit(8'h30);
                emit(8'ha9);emit(0);emit(0);emit(8'ha2);emit(0);emit(2);emit(8'h58);
                emit(8'h4c);emit(8'h80);emit(8'he0);
                cursor=122880+128;
                emit(8'haf);emit(0);emit(8'h20);emit(8'he1); // LDA long $E12000.
                emit(8'hca);emit(8'hd0);emit(8'hf9);emit(8'hdb);
                start_machine();
                dut.machine_i.slowram.g_bank[4].ram.mem[8192]=8'h5c;dut.machine_i.slowram.g_bank[4].ram.mem[8193]=8'h3e;
                beam_sync();pause_request=0;polls=0;
                while(!dut.machine_i.cpu.STPExec && polls<2000000) begin @(negedge core_clk);polls++;end
                pause_request=1;drain();
                if(!dut.machine_i.cpu.STPExec || loop_elapsed==0 || dut.machine_i.cpu.A!==16'h3e5c || dut.machine_i.cpu.X!==0)
                    $fatal(1,"mega2_load_kernel architectural state incorrect A%04x X%04x",dut.machine_i.cpu.A,dut.machine_i.cpu.X);
                // At maximum every other advance takes one primary tick and each
                // fast Mega II read three: the read waits for its arm and data edges.
                if(speed==6 && (loop_slow_reads!=992 || loop_elapsed!=loop_advances+2*loop_slow_reads))
                    $fatal(1,"Mega II fast read is not three primary ticks ticks%0d advances%0d reads%0d",loop_elapsed,loop_advances,loop_slow_reads);
                if(speed==0 && loop_slow_reads!=0) $fatal(1,"stock Mega II load used the fast slow-RAM path");
                $display("MEASURE mega2_load_kernel speed=%0d primary_ticks=%0d cpu_advances=%0d loads=1024 fast_reads=%0d",speed,loop_elapsed,loop_advances,loop_slow_reads);
            end
            mark("real 65816 bank-E1 load workload returns exact Mega II data at stock and maximum");
        end
    endtask
    // HDD wait/complete helpers for the real-CPU DMA program.
    task wait_media;
        integer polls;
        begin
            polls=0;
            while(!media_request_valid && polls<4000000) begin @(negedge core_clk);polls++;end
            if(!media_request_valid || media_request_write || media_request_lba!=1)
                $fatal(1,"HDD DMA fixture request mismatch write%0b lba%0d",media_request_write,media_request_lba);
        end
    endtask
    task complete_media;
        begin
            @(negedge core_clk);media_complete_request_id=media_request_id;
            media_complete_generation=media_request_generation;media_complete_status=0;
            media_complete_block_count=media_request_block_count;media_complete=1;
            @(negedge core_clk);media_complete=0;
        end
    endtask
    task st_abs(input [15:0] a);begin emit(8'h8d);emit(a[7:0]);emit(a[15:8]);end endtask
    task ld_imm(input [7:0] v);begin emit(8'ha9);emit(v);end endtask
    task ld_abs(input [15:0] a);begin emit(8'had);emit(a[7:0]);emit(a[15:8]);end endtask
    // Real 65816 program: copy the synthetic slot-7 ROM page with LDA $C700,X
    // into bank 00, then DMA one sector into E1:2000 (direct Mega II) and one
    // into 00:2000 (shadowed to E0:2000). Checked at stock and maximum speed.
    task dma_program;
        integer polls,first_row;
        reg [7:0] expected;
        begin
            pause_request=1;forced_vda=0;
            release dut.machine_i.cpu_addr;release dut.machine_i.cpu_vpa;release dut.machine_i.cpu_vda;
            release dut.machine_i.cpu_we_n;release dut.machine_i.cpu_dout;release dut.machine_i.cpu_vpb;
            release dut.machine_i.cpu_i_flag;
            for(integer setting=0;setting<2;setting++) begin
                speed=setting==0 ? 0:6;
                for(integer i=0;i<131072;i++) rom[i]=8'hea;
                rom[131068]=0;rom[131069]=8'he0;
                cursor=122880;emit(8'h78);emit(8'h58); // SEI; CLI (IRQ delay enabled, no IRQ sources).
                ld_imm(8'h80);st_abs(16'hc036);ld_imm(8'h00);st_abs(16'hc035);
                ld_imm(8'h80);st_abs(16'hc02d); // Slot 7 external ROM and registers.
                // Four copies with NOP / BIT dp fillers rotate the slot read
                // through every primary-clock phase relative to CLK14, so a
                // missing second preparation edge cannot pass by alignment.
                for(integer variant=0;variant<4;variant++) begin
                    emit(8'ha2);emit(0); // LDX #0
                    emit(8'hbd);emit(0);emit(8'hc7); // LDA $C700,X
                    if(variant&1) emit(8'hea); // NOP
                    if(variant&2) begin emit(8'h24);emit(0); end // BIT $00
                    emit(8'h9d);emit(0);emit(8'(8'h10+variant)); // STA $1000+256*variant,X
                    emit(8'he8);emit(8'hd0);emit(8'(-(9+(variant&1)+((variant&2)!=0 ? 2:0)))); // INX; BNE
                end
                ld_imm(8'h70);st_abs(16'hc0f3);ld_imm(0);st_abs(16'hc0f4);st_abs(16'hc0f7);
                ld_imm(8'h20);st_abs(16'hc0f5);ld_imm(1);st_abs(16'hc0f6);st_abs(16'hc0f2);
                ld_imm(8'he1);st_abs(16'hc037);emit(8'had);emit(8'hf0);emit(8'hc0); // DMA to E1:2000.
                ld_imm(0);st_abs(16'hc037);ld_imm(1);st_abs(16'hc0f2);emit(8'had);emit(8'hf0);emit(8'hc0); // DMA to 00:2000.
                emit(8'hdb);
                for(integer i=0;i<512;i+=8) memory[(24'h002000+i)>>3]=0;
                for(integer i=0;i<1024;i+=8) memory[(24'h001000+i)>>3]=0;
                mount_disk=1;start_machine();mount_disk=0;first_row=dma_rows;pause_request=0;
                for(integer transfer=0;transfer<2;transfer++) begin
                    wait_media();
                    if($test$plusargs("TRACE_DMA")) $display("DMA request tick%0d id%0d stp%b",primary_ticks,media_request_id,dut.machine_i.cpu.STPExec);
                    packet(8'h48,512);complete_media();
                    while(media_request_valid) @(negedge core_clk);
                    if($test$plusargs("TRACE_DMA")) $display("DMA completed tick%0d stp%b",primary_ticks,dut.machine_i.cpu.STPExec);
                end
                polls=0;
                // An accelerated CPU may reach STP before the second DMA starts.
                while(!(dut.machine_i.cpu.STPExec && dma_rows-first_row==2 && !dut.hdd_dma) && polls<4000000) begin @(negedge core_clk);polls++;end
                pause_request=1;drain();
                if(!dut.machine_i.cpu.STPExec || dma_rows-first_row!=2) $fatal(1,"DMA program did not complete rows%0d sd_ack%b phi2%b stall%b cache_stall%b req%b pending%b state%0d hdd_dma%b",dma_rows-first_row,dut.sd_ack,dut.phi2,dut.native_stall_q,dut.cache_stall,media_request_valid,dut.pending,dut.machine_i.hdd.dma_state,dut.hdd_dma);
                for(integer i=0;i<1024;i++) begin
                    expected=8'(((i&255)*11+5)&255);
                    if(memory[(24'h001000+i)>>3][((24'h001000+i)&7)*8+:8]!==expected)
                        $fatal(1,"slot-7 ROM byte %0d copy %0d copied %02x expected %02x speed%0d",i&255,i>>8,memory[(24'h001000+i)>>3][((24'h001000+i)&7)*8+:8],expected,speed);
                end
                for(integer i=0;i<512;i++) begin
                    expected=8'((i*7+19)&255);
                    if(dut.machine_i.slowram.g_bank[4].ram.mem[8192+i]!==expected)
                        $fatal(1,"DMA E1 byte %0d got %02x speed%0d",i,dut.machine_i.slowram.g_bank[4].ram.mem[8192+i],speed);
                    if(dut.machine_i.slowram.g_bank[0].ram.mem[8192+i]!==expected ||
                       memory[(24'h002000+i)>>3][((24'h002000+i)&7)*8+:8]!==expected)
                        $fatal(1,"DMA shadowed byte %0d mirror %02x DDR %02x speed%0d",i,dut.machine_i.slowram.g_bank[0].ram.mem[8192+i],
                            memory[(24'h002000+i)>>3][((24'h002000+i)&7)*8+:8],speed);
                end
                $display("MEASURE dma_e1 speed=%0d bytes=%0d primary_ticks=%0d min_delta=%0d max_delta=%0d",speed,
                    dma_row_bytes[first_row&7],dma_row_ticks[first_row&7],dma_row_min[first_row&7],dma_row_max[first_row&7]);
                $display("MEASURE dma_shadow speed=%0d bytes=%0d primary_ticks=%0d min_delta=%0d max_delta=%0d",speed,
                    dma_row_bytes[(first_row+1)&7],dma_row_ticks[(first_row+1)&7],dma_row_min[(first_row+1)&7],dma_row_max[(first_row+1)&7]);
            end
            mark("real 65816 slot-7 ROM copy and HDD DMA into E1 and shadowed bank 00 at stock and maximum");
        end
    endtask
    // Real 65816 sound GLU program at accelerated speeds 3 and 6. The GLU runs
    // one host access per DOC oscillator slot (every 16 CLK14), so without a
    // native hold back-to-back C03E/C03D stores and an auto-increment wave
    // upload outrun it. DOC registers and wave RAM must hold every byte.
    // The program then reads them back with the GLU read protocol (a read
    // returns the previous fetch and starts the next): a dummy and a real
    // $C03D read per DOC register, then an auto-increment wave RAM read.
    // A read that outruns the GLU returns a stale value.
    function automatic [7:0] doc_freq(input integer k);doc_freq=8'(8'h21+k*13);endfunction
    function automatic [7:0] doc_ptr(input integer k);doc_ptr=8'(8'hc3^(k*7));endfunction
    integer glu_holds=0;
    always @(posedge core_clk) if(!dut.machine_reset && dut.request_tick && dut.native_prepared && dut.cpu_native_hold) glu_holds++;
    task sound_program;
        integer polls,holds_before;
        reg [7:0] got;
        begin
            pause_request=1;forced_vda=0;
            release dut.machine_i.cpu_addr;release dut.machine_i.cpu_vpa;release dut.machine_i.cpu_vda;
            release dut.machine_i.cpu_we_n;release dut.machine_i.cpu_dout;release dut.machine_i.cpu_vpb;
            release dut.machine_i.cpu_i_flag;
            for(integer setting=0;setting<2;setting++) begin
                speed=setting==0 ? 3:6;
                for(integer i=0;i<131072;i++) rom[i]=8'hea;
                rom[131068]=0;rom[131069]=8'he0;
                cursor=122880;emit(8'h78);emit(8'h58); // SEI; CLI (no IRQ sources): keep acceleration.
                ld_imm(0);st_abs(16'hc03c); // DOC registers, no auto-increment.
                for(integer k=0;k<8;k++) begin ld_imm(8'(k));st_abs(16'hc03e);ld_imm(doc_freq(k));st_abs(16'hc03d);end
                for(integer k=0;k<8;k++) begin ld_imm(8'(8'h80+k));st_abs(16'hc03e);ld_imm(doc_ptr(k));st_abs(16'hc03d);end
                ld_imm(8'h60);st_abs(16'hc03c); // Wave RAM, auto-increment.
                ld_imm(0);st_abs(16'hc03e);ld_imm(8'h10);st_abs(16'hc03f);
                emit(8'ha2);emit(0); // LDX #0
                emit(8'h8a);emit(8'h49);emit(8'h5a); // TXA; EOR #$5A
                st_abs(16'hc03d);emit(8'he8);emit(8'hd0);emit(8'hf7); // STA $C03D; INX; BNE
                ld_imm(0);st_abs(16'hc03c); // DOC registers, no auto-increment.
                for(integer k=0;k<16;k++) begin
                    ld_imm(k<8 ? 8'(k) : 8'(8'h80+k-8));st_abs(16'hc03e);
                    ld_abs(16'hc03d);ld_abs(16'hc03d);st_abs(16'(16'h0300+k));
                end
                ld_imm(8'h60);st_abs(16'hc03c); // Wave RAM, auto-increment.
                ld_imm(0);st_abs(16'hc03e);ld_imm(8'h10);st_abs(16'hc03f);
                ld_abs(16'hc03d); // Dummy read: fetch $1000.
                emit(8'ha2);emit(0); // LDX #0
                ld_abs(16'hc03d);emit(8'h9d);emit(8'h00);emit(8'h60); // LDA $C03D; STA $6000,X
                emit(8'he8);emit(8'hd0);emit(8'hf7); // INX; BNE
                emit(8'hdb);
                start_machine();
                for(integer i=0;i<256;i++) dut.machine_i.snd.ram.ram[16'h1000+i]=8'h00;
                for(integer i=0;i<2;i++) memory[(24'h000300>>3)+i]=64'h0;
                for(integer i=0;i<32;i++) memory[(24'h006000>>3)+i]=64'h0;
                holds_before=glu_holds;pause_request=0;polls=0;
                while(!dut.machine_i.cpu.STPExec && polls<4000000) begin @(negedge core_clk);polls++;end
                pause_request=1;repeat(200) @(negedge core_clk);drain();
                if(!dut.machine_i.cpu.STPExec) $fatal(1,"sound GLU program did not finish speed%0d",speed);
                if(dut.cpu_speed!=3'(speed)) $fatal(1,"sound GLU program ran at speed%0d expected%0d",dut.cpu_speed,speed);
                for(integer k=0;k<8;k++) begin
                    if(dut.machine_i.snd.doc.r_freq_low[k]!==doc_freq(k))
                        $fatal(1,"sound GLU DOC register %02x got %02x expected %02x speed%0d",k,dut.machine_i.snd.doc.r_freq_low[k],doc_freq(k),speed);
                    if(dut.machine_i.snd.doc.r_table_ptr[k]!==doc_ptr(k))
                        $fatal(1,"sound GLU DOC register %02x got %02x expected %02x speed%0d",8'h80+k,dut.machine_i.snd.doc.r_table_ptr[k],doc_ptr(k),speed);
                end
                for(integer i=0;i<256;i++) begin
                    got=dut.machine_i.snd.ram.ram[16'h1000+i];
                    if(got!==8'(i^8'h5a)) $fatal(1,"sound GLU wave RAM %04x got %02x expected %02x speed%0d",16'h1000+i,got,8'(i^8'h5a),speed);
                end
                if(dut.machine_i.snd.ram.ram[16'h1100]!==8'h00) $fatal(1,"sound GLU wave RAM upload overran speed%0d",speed);
                for(integer k=0;k<16;k++) begin
                    got=memory[(24'h000300+k)>>3][((24'h000300+k)&7)*8+:8];
                    if(got!==(k<8 ? doc_freq(k) : doc_ptr(k-8)))
                        $fatal(1,"sound GLU read-back of DOC register %02x got %02x expected %02x speed%0d",
                            k<8 ? k : 8'h80+k-8,got,k<8 ? doc_freq(k) : doc_ptr(k-8),speed);
                end
                for(integer i=0;i<256;i++) begin
                    got=memory[(24'h006000+i)>>3][((24'h006000+i)&7)*8+:8];
                    if(got!==8'(i^8'h5a)) $fatal(1,"sound GLU read-back of wave RAM %04x got %02x expected %02x speed%0d",16'h1000+i,got,8'(i^8'h5a),speed);
                end
                if(glu_holds==holds_before) $fatal(1,"sound GLU program never waited for the GLU speed%0d",speed);
                $display("MEASURE sound_glu speed=%0d holds=%0d",speed,glu_holds-holds_before);
            end
            mark("real 65816 sound GLU register writes, auto-increment wave upload and read-back exact at speeds 3 and 6");
        end
    endtask
    initial begin
        for(integer i=0;i<131072;i++) rom[i]=8'hea;
        rom[131068]=0;rom[131069]=8'he0;
        force dut.machine_i.cpu_addr=forced_addr;force dut.machine_i.cpu_vpa=forced_vpa;
        force dut.machine_i.cpu_vda=forced_vda;force dut.machine_i.cpu_we_n=forced_we_n;
        force dut.machine_i.cpu_dout=forced_data;force dut.machine_i.cpu_vpb=forced_vpb_n;
        force dut.machine_i.cpu_i_flag=1'b0;
        pause_request=1;start_machine();
        if($test$plusargs("ONLY_DMA")) dma_program();
        else if($test$plusargs("ONLY_KERNELS")) begin real_cpu_kernel();native_write_kernel();load_kernel();dma_program();end
        else if($test$plusargs("ONLY_MOTOR")) motor_checks();
        else if($test$plusargs("ONLY_IWM_MODE")) iwm_mode_checks();
        else if($test$plusargs("ONLY_SOUND")) sound_program();
        else if($test$plusargs("ONLY_JOYSTICK")) joystick_checks();
        else begin
            if(!$test$plusargs("ONLY_SHADOW") && !$test$plusargs("ONLY_PADDLE")) joystick_checks();
            if(!$test$plusargs("ONLY_SHADOW")) paddle_checks();
            if(!$test$plusargs("ONLY_PADDLE")) shadow_checks();
            if(!$test$plusargs("ONLY_PADDLE") && !$test$plusargs("ONLY_SHADOW")) mega2_mapping_checks();
            if(!$test$plusargs("ONLY_PADDLE") && !$test$plusargs("ONLY_SHADOW")) begin iwm_mode_checks();motor_checks();real_cpu_kernel();native_write_kernel();load_kernel();sound_program();dma_program();end
        end
        $display("CHECKS completed=%0d",cases);$finish;
    end
    initial begin #1000000000;$fatal(1,"acceleration gate test timeout");end
endmodule


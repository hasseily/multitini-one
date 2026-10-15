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
    wire memory_valid,memory_ready;reg [63:0] memory_rdata=0;reg memory_rvalid=0,memory_fault=0;
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
    reg busy=0,pending_write=0;reg [23:0] pending_addr;reg [63:0] pending_data;reg [7:0] pending_mask;
    integer delay_count=0,writes=0,target_writes=0;
    assign memory_ready=!busy;
    always @(posedge core_clk)begin
        memory_rvalid<=0;
        if(memory_valid && memory_ready)begin
            pending_addr<=memory_addr;pending_data<=memory_wdata;pending_mask<=memory_wstrb;
            pending_write<=memory_write;delay_count<=3+memory_addr[5:3];busy<=1;
        end else if(busy)begin
            if(delay_count!=0)delay_count<=delay_count-1;
            else begin
                if(pending_write)begin
                    for(integer lane=0;lane<8;lane=lane+1)
                        if(pending_mask[lane])memory[pending_addr[23:3]][lane*8+:8]<=pending_data[lane*8+:8];
                    writes<=writes+1;
                    if(pending_addr[23:3]==(24'h002000>>3)) target_writes<=target_writes+1;
                end
                memory_rdata<=memory[pending_addr[23:3]];memory_rvalid<=1;busy<=0;
            end
        end
    end
    reg [7:0] rom[0:131071];
    integer speed=6,cursor=0,cases=0,slow_writes=0,native_commits=0,paddle_triggers=0;
    reg [23:0] forced_addr=0;
    reg forced_we_n=1,forced_vda=0,forced_vpa=0,forced_vpb_n=1;
    reg [7:0] forced_data=0;
    longint unsigned primary_ticks=0;
    always @(posedge core_clk) primary_ticks++;
    always @(posedge dut.clk14) if(!dut.machine_reset) begin
        if(dut.machine_i.bus_phi2) native_commits++;
        if(dut.machine_i.slowram_ce && dut.machine_i.slowram_we && dut.machine_i.bus_phi2) slow_writes++;
        if(dut.machine_i.paddle_trigger) paddle_triggers++;
    end
    task emit(input byte value);begin rom[cursor]=value;cursor++;end endtask
    task packet(input byte kind,input integer size);
        byte value;
        begin
            load_epoch=~load_epoch;
            for(integer i=0;i<size;i++) begin
                value=kind==8'h41 ? rom[i] : kind==8'h43 && i==1 ? speed : 0;
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
    task motor_checks;
        bit expected_native;
        integer motor_checks_count;
        begin
            // A real empty-drive motor command still invokes the separate
            // two-ms IWM access delay and remains visible to the controller.
            side_read(24'h00c0e9);
            if(dut.machine_i.iwm_holdoff==0 || dut.cpu_speed!=0 || !dut.machine_i.iwmc.drive_on)
                $fatal(1,"empty-drive IWM access lost native command/delay");
            repeat(4*28636+8) @(negedge core_clk);
            if(dut.machine_i.iwm_holdoff!=0) $fatal(1,"IWM access delay failed to expire");
            force dut.machine_i.iwmc.motor_spinning=test_motor525;
            force dut.machine_i.iwmc.drive35_motor_spinning=test_motor35;
            force dut.machine_i.DISK_READY=test_disks;
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
            mark("empty-drive motor latch stays visible without global clamp; mounted-family timing and IWM delay retained");
        end
    endtask
    task start_machine;
        begin
            reset=1;core_reset_request=1;
            repeat(20) @(negedge core_clk);reset=0;
            packet(8'h43,16);packet(8'h41,131072);packet(8'h42,4096);packet(8'h46,4096);packet(8'h45,256);
            if(!rom_loaded) $fatal(1,"synthetic ROM fixtures not loaded");
            core_reset_request=0;
            while(dut.machine_reset || dut.machine_reset_raw) @(negedge core_clk);
        end
    endtask
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
            @(negedge core_clk);pause_request=1;forced_vda=0;
            if(check && received!==expected) $fatal(1,"gate read data address%06x got%02x expected%02x",address,received,expected);
            if(native_commits-commits_before!=integer'(expected_native)) $fatal(1,"native completion count address%06x count%0d",address,native_commits-commits_before);
            if(wr && address==24'h002000 && (slow_writes-slow_before!=1 || writes-ddr_before!=1))
                $fatal(1,"shadow write must update DDR and slow mirror exactly once");
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
    task mark(input string name);begin cases++;$display("PASS: acceleration gates %s",name);end endtask

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
            // Independent expectation for ordinary populated RAM: a read is
            // fast in every shadow mode; a write is native exactly when the
            // actual slow mirror is enabled. E0/E1 remain direct native RAM.
            for(integer rom_mode=0;rom_mode<2;rom_mode++) for(integer shadow=0;shadow<128;shadow++)
                for(integer allbank=0;allbank<2;allbank++) begin
                    test_shadow=shadow;test_rom=rom_mode;test_cya=allbank ? 8'h90:8'h80;
                    write_byte(24'he0c035,test_shadow);write_byte(24'he0c036,test_cya);
                    forced_vda=1;
                    for(integer bank=0;bank<4;bank++)
                    for(integer address=0;address<12;address++) for(integer wr=0;wr<2;wr++) begin
                        forced_addr={8'(bank),addresses[address]};forced_we_n=!wr;#0.001;
                        expected_fast=!wr || !dut.machine_i.slowram_ce;
                        if(!dut.machine_i.fastram_ce || dut.cpu_fast_cycle!==expected_fast)
                            $fatal(1,"RAM shadow class bank%0d addr%04x shadow%02x ROM%0b allbank%0b wr%0b fast%0b",bank,addresses[address],test_shadow,test_rom,allbank,wr,dut.cpu_fast_cycle);
                        decode_checks++;
                    end
                end
            for(integer bank=224;bank<=225;bank++) for(integer addr=0;addr<65536;addr+=256) begin
                forced_addr={8'(bank),16'(addr)};forced_we_n=1;#0.001;
                if(dut.cpu_fast_cycle) $fatal(1,"direct Mega II RAM/IO/ROM escaped native bridge");
                decode_checks++;
            end
            release dut.machine_i.rom_select;
            forced_vda=0;
            $display("COMPARE shadow_ram=%0d",decode_checks);
            mark("all shadow/config RAM reads fast; mirrored writes and E0/E1 native");
            // Real production bridge writes must preserve both memories, and
            // their later fast vs slow reads must return their own exact copy.
            write_byte(24'he0c035,0);write_byte(24'he0c036,8'h80);
            write_byte(24'h002000,8'h5a);
            if(memory[24'h002000>>3][7:0]!==8'h5a) $fatal(1,"shadow write lost main DDR copy");
            read_byte(24'h002000,8'h5a,0);read_byte(24'he02000,8'h5a,1);
            write_byte(24'he02000,8'ha6);
            read_byte(24'h002000,8'h5a,0);read_byte(24'he02000,8'ha6,1);
            write_byte(24'h016000,8'hb7);
            read_byte(24'h016000,8'hb7,0);read_byte(24'he16000,8'hb7,1);
            mark("native shadow writes retire once and preserve DDR/slow-RAM mirrors");
        end
    endtask

    longint unsigned loop_start=0,loop_advances=0,loop_elapsed=0;
    bit measuring=0,write_kernel=0;
    reg [15:0] kernel_start=16'h2000,kernel_end=16'h2004,kernel_warm_x=16'h0f00;
    always @(posedge core_clk) if(!dut.machine_reset && dut.cpu_ce && !pause_request && !forced_vda) begin
        if(dut.machine_i.cpu.STATE==0 && dut.machine_i.cpu.PBR==0 && dut.machine_i.cpu.PC==kernel_start && dut.machine_i.cpu.X==kernel_warm_x) begin
            measuring=1;loop_start=primary_ticks;loop_advances=0;
        end
        if(measuring) begin
            if(dut.machine_i.pdl_holdoff!=0) $fatal(1,"real IRQ dispatch ROM fetch armed paddle delay");
            if(dut.machine_i.cpu.STATE==0 && dut.machine_i.cpu.PC==kernel_end) begin
                measuring=0;loop_elapsed=primary_ticks-loop_start;
            end else loop_advances++;
        end
    end
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
                start_machine();pause_request=0;polls=0;
                while(!dut.machine_i.cpu.STPExec && polls<2000000) begin @(negedge core_clk);polls++;end
                pause_request=1;
                if(!dut.machine_i.cpu.STPExec || loop_elapsed==0 || dut.machine_i.cpu.A!==16'h2234 || dut.machine_i.cpu.X!==0 ||
                   memory[24'h020100>>3][15:0]!==16'h2234) $fatal(1,"real shadow-RAM kernel architectural state incorrect");
                if(speed==6 && loop_elapsed!=loop_advances) $fatal(1,"warm shadow RAM kernel did not reach one primary tick per CPU advance ticks%0d advances%0d",loop_elapsed,loop_advances);
                $display("MEASURE shadow_kernel speed=%0d primary_ticks=%0d cpu_advances=%0d",speed,loop_elapsed,loop_advances);
            end
            mark("real 65816 IRQ-ROM-to-shadow-RAM loop exact stock/max execution and 57MHz warm cadence");
        end
    endtask
    task native_write_kernel;
        integer polls,slow_before,ddr_before;
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
                emit(8'h8f);emit(0);emit(8'h20);emit(0); // STA $002000, native shadow write.
                emit(8'hca);emit(8'hd0);emit(8'hf8);emit(8'hdb);
                start_machine();slow_before=slow_writes;ddr_before=target_writes;pause_request=0;polls=0;
                while(!dut.machine_i.cpu.STPExec && polls<2000000) begin @(negedge core_clk);polls++;end
                pause_request=1;
                if(!dut.machine_i.cpu.STPExec || loop_elapsed==0 || dut.machine_i.cpu.A!==16'h1434 || dut.machine_i.cpu.X!==0 ||
                   memory[24'h002000>>3][15:0]!==16'h1434 ||
                   dut.machine_i.slowram.g_bank[0].ram.mem[8192]!==8'h34 || dut.machine_i.slowram.g_bank[0].ram.mem[8193]!==8'h14 ||
                   slow_writes-slow_before!=1024 || target_writes-ddr_before!=1024)
                    $fatal(1,"native shadow-store kernel lost data or single write retirement slow%0d DDR%0d",slow_writes-slow_before,target_writes-ddr_before);
                $display("MEASURE native_write_kernel speed=%0d primary_ticks=%0d cpu_advances=%0d stores=1024",speed,loop_elapsed,loop_advances);
            end
            mark("real 65816 native shadow-store workload preserves 1024 exact writes per rate");
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
        if($test$plusargs("ONLY_MOTOR")) motor_checks();
        else begin
            if(!$test$plusargs("ONLY_SHADOW")) paddle_checks();
            if(!$test$plusargs("ONLY_PADDLE")) shadow_checks();
            if(!$test$plusargs("ONLY_PADDLE") && !$test$plusargs("ONLY_SHADOW")) begin motor_checks();real_cpu_kernel();native_write_kernel();end
        end
        $display("CHECKS completed=%0d",cases);$finish;
    end
    initial begin #1000000000;$fatal(1,"acceleration gate test timeout");end
endmodule


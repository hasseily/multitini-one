// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// API qualification with an ideal byte memory, not the production cache.
module tb_gs_external_cpu;
    reg primary=0;always #8.730 primary=~primary;
    reg [1:0] phase=0;reg native_clk=0;
    always @(posedge primary)begin phase<=phase+1'b1;native_clk<=phase[1];end
    reg reset=1;reg [2:0] host_speed=6;
    reg cpu_ce=0,native_commit=0,waiting=0,seen_return=0;
    wire [7:0] native_read_data,cpu_local_data;
    wire native_return_toggle,cpu_fast_cycle,cpu_local_read;
    wire cpu_slowram_read,cpu_slowram_write,cpu_slowram_ready,cpu_slowram_pending,cpu_native_two_edge,cpu_native_hold;
    wire [7:0] cpu_slowram_data;
    wire [2:0] cpu_speed;wire phi2,cpu_valid;
    wire [23:0] top_addr;wire [7:0] top_dout;wire we,fastram_ce,rom_ce;
    reg [7:0] rom[0:65535],ram[0:65535];
    wire [7:0] top_din=rom_ce ? rom[top_addr[15:0]] : ram[top_addr[15:0]];
    wire [7:0] cpu_din=waiting ? native_read_data : cpu_local_read ? cpu_local_data :
        cpu_slowram_ready ? cpu_slowram_data : top_din;
    iigs #(.EXTERNAL_CPU(1),.MAX_ACCEL_SPEED(6)) dut(
        .reset(reset),.cold_reset(reset),.CPU_CLK(primary),.CPU_CE(cpu_ce),.CPU_PUBLISH(phase==0),.CPU_DIN(cpu_din),
        .NATIVE_COMMIT(native_commit),
        .CPU_FAST_CE(cpu_ce && !waiting),.NATIVE_EDGE(phase==2),.cpu_slowram_read(cpu_slowram_read),
        .cpu_slowram_write(cpu_slowram_write),.cpu_slowram_ready(cpu_slowram_ready),.cpu_slowram_data(cpu_slowram_data),
        .cpu_slowram_pending(cpu_slowram_pending),.cpu_native_hold(cpu_native_hold),
        .cpu_native_two_edge(cpu_native_two_edge),.cpu_speed(cpu_speed),.cpu_fast_cycle(cpu_fast_cycle),
        .cpu_local_read(cpu_local_read),.cpu_local_data(cpu_local_data),.native_read_data(native_read_data),
        .native_return_toggle(native_return_toggle),.CLK_14M(native_clk),.CLK_28M(1'b0),.clk_vid(primary),.ce_pix(phase==0),
        .load_rom_clk(primary),.load_rom_we(1'b0),.load_rom_addr(16'b0),.load_rom_data(8'b0),
        .top_addr(top_addr),.top_dout(top_dout),.top_din(top_din),.we(we),.fastram_ce(fastram_ce),.rom_ce(rom_ce),
        .host_speed(host_speed),.accel_capable(1'b1),.zip_regs_en(1'b1),.mem_stall(1'b0),
        .osd_spkr_delay(1'b0),.osd_pdl_delay(1'b0),.osd_ctr_delay(1'b0),.osd_cps_follow(1'b0),.osd_irq_delay(1'b1),
        .osd_twgs_gfx(1'b0),.osd_twgs_snd(1'b0),.twgs_present(1'b0),.rom_select(1'b1),
        .timestamp(33'b0),.ps2_key(11'b0),.ps2_mouse(25'b0),.selftest_override(1'b0),
        .nv_wr(1'b0),.nv_addr(9'b0),.nv_din(8'b0),.phi2(phi2),.cpu_valid(cpu_valid),
        .HDD_MOUNTED(2'b0),.HDD_ACK(2'b0),.HDD_RAM_WE(1'b0),.HDD_RAM_ADDR(9'b0),.HDD_RAM_DI(8'b0),
        .img_size(64'b0),.img_readonly(1'b1),.serial_loopback(1'b0),.UART_RXD(1'b1),.UART_CTS(1'b0),
        .joystick_0(32'b0),.joystick_1(32'b0),.joystick_l_analog_0(16'b0),.joystick_l_analog_1(16'b0));
    longint ticks=0,measured_ticks=0,loop_start=0,advances=0;
    integer period=1,pace=0,commits=0,returns=0,local_reads=0,fast_stores=0;
    integer text_writes=0,doc_writes=0,scc_writes=0,prtc_writes=0,prtc_control_writes=0,slow_writes=0;
    reg measuring=0,measured=0,prev_toggle=0;
    // Drive before the primary edge, with native commits prepared a full
    // primary period before their child-clock edge. Return is consumed later.
    always @(negedge primary)begin
        cpu_ce=0;
        if(reset)begin native_commit=0;waiting=0;seen_return=0;pace=0;end
        else begin
            if(phase==0)native_commit=0;
            if(waiting)begin
                if(native_return_toggle!=seen_return)cpu_ce=1;
            end else if(cpu_fast_cycle && cpu_speed!=0)begin
                // Direct Mega II reads wait for the registered slow-RAM read; no
                // store retires on the NATIVE_EDGE that defers a staged write.
                if(cpu_slowram_read && !cpu_slowram_ready);
                else if(we && cpu_slowram_pending && phase==2);
                else if(pace==0)begin cpu_ce=1;pace=period-1;end else pace=pace-1;
            end else if(phase==2 && phi2 && !cpu_native_hold)begin native_commit=1;waiting=1;end
        end
    end
    always @(posedge primary)begin
        ticks=ticks+1;
        if(reset)begin prev_toggle=0;end
        else begin
            if(dut.cpu_i_flag && dut.zip_irq_delay_en && cpu_speed!=0)
                $fatal(1,"SEI did not restore native IRQ compatibility speed");
            if(native_return_toggle!=prev_toggle)begin returns=returns+1;prev_toggle=native_return_toggle;end
            if(cpu_ce)begin
                if(waiting)begin seen_return<=native_return_toggle;waiting<=0;end
                if(we && fastram_ce)begin ram[top_addr[15:0]]<=top_dout;fast_stores=fast_stores+1;end
                if(cpu_local_read)local_reads=local_reads+1;
                if(dut.cpu.STATE==0 && dut.cpu.PC==16'he100 && dut.cpu.X==16'd3840 && !measured)begin
                    measuring=1;loop_start=ticks;advances=0;
                end
                if(measuring)begin
                    if(dut.cpu.STATE==0 && dut.cpu.PC==16'he105)begin
                        measured_ticks=ticks-loop_start;measuring=0;measured=1;
                    end else advances=advances+1;
                end
            end
        end
    end
    // Count actual slow-RAM port writes on that port's own (primary) clock:
    // native commits and fast Mega II stores must each write exactly once,
    // from the staged tuple and never on the native (video read) edge.
    always @(posedge primary)if(!reset && dut.slowram.enable_a && dut.slowram.wren_a)begin
        slow_writes=slow_writes+1;
        if(phase==2)$fatal(1,"slow-RAM port A wrote on the native edge");
    end
    reg [7:0] last_text=0;reg [15:0] last_sound_addr=0;
    always @(posedge native_clk)begin
        if(!reset)begin
            if(native_commit)begin
                commits=commits+1;
                if(dut.IO && we)begin
                    case(dut.addr_bef)
                        16'hc022:text_writes=text_writes+1;
                        16'hc03e,16'hc03f:doc_writes=doc_writes+1;
                        16'hc038:scc_writes=scc_writes+1;
                        16'hc033:prtc_writes=prtc_writes+1;
                        16'hc034:prtc_control_writes=prtc_control_writes+1;
                    endcase
                end
            end
            if(!native_commit && (dut.TEXTCOLOR!==last_text || dut.snd.glu.sound_addr!==last_sound_addr))
                $fatal(1,"native write changed state without commit");
        end
        #0.001;last_text=dut.TEXTCOLOR;last_sound_addr=dut.snd.glu.sound_addr;
    end
    // Check architectural state remains held on every primary cycle with CE0,
    // including every CLK14 edge. Interrupt synchronizers may run independently.
    reg [71:0] held;reg old_ce;
    always @(posedge primary)begin
        held={dut.cpu.PC,dut.cpu.A,dut.cpu.X,dut.cpu.Y,dut.cpu.STATE,dut.cpu.P[3:0]};old_ce=cpu_ce;
        #0.001;
        if(!reset && !old_ce && {dut.cpu.PC,dut.cpu.A,dut.cpu.X,dut.cpu.Y,dut.cpu.STATE,dut.cpu.P[3:0]}!==held)
            $fatal(1,"native clock advanced external CPU without CPU_CE");
    end
    integer cursor;
    task emit(input [7:0] b);rom[cursor++]=b;endtask
    task lda8(input [7:0] b);emit('hA9);emit(b);endtask
    task sta(input [15:0] a);emit('h8D);emit(a[7:0]);emit(a[15:8]);endtask
    task lda(input [15:0] a);emit('hAD);emit(a[7:0]);emit(a[15:8]);endtask
    task store_result(input [7:0] a);emit('h8F);emit(a);emit(0);emit(2);endtask
    task make_program;
        for(integer i=0;i<65536;i=i+1)begin rom[i]='hEA;ram[i]=0;end
        rom['hFFFC]=0;rom['hFFFD]='hE0;cursor='hE000;
        emit('h78);lda8('h80);sta('hC036); // native fast base
        lda8('hF3);sta('hC022);lda('hC022);store_result('h10);
        lda8('h34);sta('hC03E);lda8('h12);sta('hC03F);
        lda('hC03E);store_result('h11);lda('hC03F);store_result('h12);
        lda8('h5A);sta('hC033);lda('hC033);store_result('h13);
        // Execute a real RTC protocol transaction; an echo from C033 alone
        // cannot detect a missing C034 control-register select.
        lda8('h40);sta('hC033);lda8('h80);sta('hC034); // Select PRAM byte 0.
        lda8('h5A);sta('hC033);lda8('h80);sta('hC034); // Commit its data.
        lda8('hC0);sta('hC033);lda8('h80);sta('hC034);
        lda8('hC0);sta('hC034);lda('hC033);store_result('h17);
        // C032 belongs to the IRQ controller and must not alias RTC control.
        lda8(3);sta('hC034);lda8('h1C);sta('hC032);lda('hC034);store_result('h18);
        lda8(12);sta('hC038);lda8('hA6);sta('hC038);lda8(12);sta('hC038);lda('hC038);store_result('h14);
        lda8('h5A);emit('h8F);emit(0);emit(4);emit('hE0); // native slow RAM
        emit('hAF);emit(0);emit(4);emit('hE0);store_result('h15);
        lda8('h50);repeat(4)sta('hC05A);lda8(0);sta('hC05D);sta('hC05B);
        lda('hC05B);store_result('h16); // accelerator-own read uses local fast path
        emit('h18);emit('hFB);emit('h18);emit('hC2);emit('h30);
        emit('hA2);emit(0);emit('h10);emit('hA0);emit(0);emit(0);emit('hA9);emit('h34);emit('h12);emit('h58);
        if(cursor>'hE100)$fatal(1,"synthetic program overlaps measurement loop");
        while(cursor<'hE100)emit('hEA);
        emit('h1A);emit('hC8);emit('hCA);emit('hD0);emit('hFB);
        store_result('h20);emit('hDB);
    endtask
    initial begin
        for(integer speed=5;speed<=6;speed=speed+1)begin
            reset=1;host_speed=speed[2:0];period=speed==5 ? 2 : 1;
            commits=0;returns=0;local_reads=0;fast_stores=0;text_writes=0;doc_writes=0;scc_writes=0;prtc_writes=0;prtc_control_writes=0;slow_writes=0;
            measuring=0;measured=0;make_program();repeat(40)@(negedge primary);reset=0;
            begin integer timeout=0;while(!dut.cpu.STPExec && timeout<1000000)begin @(negedge primary);timeout++;end end
            if(!dut.cpu.STPExec || !measured)$fatal(1,"external CPU timeout PC=%x",dut.cpu.PC);
            repeat(8)@(negedge primary);
            if(ram['h10]!==8'hF3 || ram['h11]!==8'h34 || ram['h12]!==8'h12 || ram['h13]!==8'h5A ||
               ram['h14]!==8'hA6 || ram['h15]!==8'h5A || (ram['h16]&8'h50)!==8'h40 ||
               ram['h17]!==8'h5A || ram['h18]!==8'h03 || dut.prtc.pram[0]!==8'h5A ||
               {ram['h21],ram['h20]}!==16'h2234 || dut.cpu.X!==0 || dut.cpu.Y!==16'h1000)
                $fatal(1,"native peripheral/CPU outcome failed text=%x doc=%x%x prtc=%x pram=%x readback=%x rtc_control=%x scc=%x slow=%x zip=%x sum=%x%x",
                    ram['h10],ram['h12],ram['h11],ram['h13],dut.prtc.pram[0],ram['h17],ram['h18],ram['h14],ram['h15],ram['h16],ram['h21],ram['h20]);
            if(commits!=returns || text_writes!=1 || doc_writes!=2 || scc_writes!=3 || prtc_writes!=4 || prtc_control_writes!=5 || slow_writes!=1 || local_reads!=1)
                $fatal(1,"native completion accounting failed commits=%0d returns=%0d text=%0d doc=%0d scc=%0d prtc=%0d rtc_control=%0d slow=%0d local=%0d",
                    commits,returns,text_writes,doc_writes,scc_writes,prtc_writes,prtc_control_writes,slow_writes,local_reads);
            // Software 100% intentionally selects installed max6 even when host
            // starts5. This API fixture independently paces CE at both rates.
            if(dut.zip_speed_code!=6 || cpu_speed!=6 || advances!=34559 || measured_ticks!=advances*period)
                $fatal(1,"external rate/software max failed speed=%0d ticks=%0d advances=%0d live=%0d",speed,measured_ticks,advances,cpu_speed);
            $display("PASS: external CPU speed=%0d ticks=%0d advances=%0d native_commits=%0d exact IO/slow RAM/ZipGS/results",speed,measured_ticks,advances,commits);
        end
        $finish;
    end
    initial begin #100000000;$fatal(1,"external CPU test timeout");end
endmodule

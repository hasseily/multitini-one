// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Synthetic bytes and an original 6809 test program only. No system/game ROM.
module tb_vectrex_transport;
    localparam WIDTH=32, HEIGHT=40, AW=$clog2(WIDTH*HEIGHT);
    reg clk=0;
    always #5 clk=~clk;
    reg reset=1, core_reset_request=0, pause_request=1, clear_load_error=0;
    reg [15:0] input_code=0, input_value=0;
    reg input_valid=0;
    wire input_ready;
    reg [7:0] load_data=0, load_kind=0;
    reg load_valid=0, load_first=0, load_last=0;
    reg load_abort=0, load_abort_epoch=0, load_epoch=0;
    reg [31:0] load_offset=0, load_total=0;
    wire load_ready, ready, load_error, bios_loaded, cart_loaded;
    wire [31:0] core_id, drawn_lines, overflow_count;
    wire signed [23:0] audio_l, audio_r;
    wire audio_valid;
    reg [AW-1:0] pixel_address=0;
    wire [7:0] pixel_intensity;
    wire [15:0] debug_cpu_address;
    wire debug_cpu_read, debug_cpu_write;
    wire [7:0] debug_cpu_data;
    wire signed [23:0] debug_beam_x, debug_beam_y;
    wire debug_beam_valid, debug_beam_blank;
    wire [7:0] debug_beam_intensity;
    multitini_vectrex_machine #(.CLOCK_HZ(24000000),.WIDTH(WIDTH),.HEIGHT(HEIGHT)) dut (.*,
        .pixel_clk(clk));
    integer failures=0, checks=0, captured_lines=0;
    reg [71:0] captured_line;
    integer n, before_lines;
    reg [15:0] paused_pc;
    reg [7:0] bios_image[0:8191];

    task check(input bit condition, input string label_text);
        begin
            checks=checks+1;
            if (!condition) begin
                failures=failures+1;
                $display("FAIL: %s at %0t",label_text,$time);
            end
        end
    endtask
    task power_cycle;
        begin
            @(negedge clk);
            reset=1;load_valid=0;load_abort=0;input_valid=0;pause_request=1;
            core_reset_request=0;clear_load_error=0;
            repeat(4) @(negedge clk);
            reset=0;
            repeat(WIDTH*HEIGHT+8) @(negedge clk);
        end
    endtask
    task clear_error;
        begin
            @(negedge clk);clear_load_error=1;
            @(negedge clk);clear_load_error=0;
        end
    endtask
    task send_byte(input [7:0] kind, input [31:0] total, offset,
                   input first, last, epoch, input [7:0] value);
        integer timeout_count;
        begin
            @(negedge clk);
            timeout_count=0;
            while (!load_ready && timeout_count<5000) begin
                @(negedge clk);timeout_count=timeout_count+1;
            end
            if(!load_ready) $fatal(1,"Loader did not become ready");
            load_kind=kind;load_total=total;load_offset=offset;
            load_first=first;load_last=last;load_epoch=epoch;
            load_data=value;load_valid=1;
            @(negedge clk);load_valid=0;load_first=0;load_last=0;
            // The public ready status is registered before its board CDC.
            // Allow that register to observe the committed/rejected byte.
            @(negedge clk);
        end
    endtask
    task mode(input [7:0] value, input epoch);
        send_byte(8'h44,1,0,1,1,epoch,value);
    endtask
    task abort_transfer(input epoch);
        begin
            @(negedge clk);load_abort=1;load_abort_epoch=epoch;
            @(negedge clk);load_abort=0;
        end
    endtask
    task load_bios;
        begin
            for(integer offset=0;offset<8192;offset=offset+1)
                send_byte(8'h40,8192,offset,offset==0,offset==8191,0,bios_image[offset]);
        end
    endtask
    task cart(input integer bytes, input epoch);
        begin
            for(integer offset=0;offset<bytes;offset=offset+1)
                send_byte(8'h41,bytes,offset,offset==0,offset==bytes-1,epoch,
                    offset<32768 ? 8'haa : 8'h55);
        end
    endtask
    task input_event(input [15:0] code,value);
        begin
            @(negedge clk);input_valid=1;input_code=code;input_value=value;
            @(negedge clk);input_valid=0;
        end
    endtask
    task arm_line(input signed [15:0] x0,y0,x1,y1, input [7:0] intensity, input epoch);
        reg [71:0] bytes;
        begin
            bytes={intensity,y1,x1,y0,x0};
            for(integer offset=0;offset<9;offset=offset+1)
                send_byte(8'h42,9,offset,offset==0,offset==8,epoch,bytes[offset*8+:8]);
        end
    endtask
    task psg_write(input [7:0] reg_number,value);
        begin
            send_byte(8'h43,2,0,1,0,0,reg_number);
            send_byte(8'h43,2,1,0,1,0,value);
            repeat(8) @(negedge clk);
        end
    endtask
    task expect_bad(input [7:0] kind,input [31:0] total,offset,
                    input first,last,input [7:0] data,input string reason);
        begin
            clear_error();
            send_byte(kind,total,offset,first,last,0,data);
            check(load_error && !ready,reason);
        end
    endtask
    always @(posedge clk) if(dut.renderer_i.push) begin
        captured_lines=captured_lines+1;
        captured_line={dut.renderer_i.line_x0,dut.renderer_i.line_y0,
            dut.renderer_i.line_x1,dut.renderer_i.line_y1,dut.renderer_i.line_intensity};
    end

    initial begin
        for(n=0;n<8192;n=n+1) bios_image[n]=8'h12;
        // Set VIA PB6 to output; read each 32 KiB bank into RAM, then loop.
        bios_image[0]=8'h86;bios_image[1]=8'h40;
        bios_image[2]=8'hb7;bios_image[3]=8'hd0;bios_image[4]=8'h02;
        bios_image[5]=8'h4f;
        bios_image[6]=8'hb7;bios_image[7]=8'hd0;bios_image[8]=8'h00;
        bios_image[9]=8'hb6;bios_image[10]=0;bios_image[11]=0;
        bios_image[12]=8'hb7;bios_image[13]=8'hc8;bios_image[14]=0;
        bios_image[15]=8'h86;bios_image[16]=8'h40;
        bios_image[17]=8'hb7;bios_image[18]=8'hd0;bios_image[19]=0;
        bios_image[20]=8'hb6;bios_image[21]=0;bios_image[22]=0;
        bios_image[23]=8'hb7;bios_image[24]=8'hc8;bios_image[25]=1;
        bios_image[26]=8'h20;bios_image[27]=8'hfe;
        bios_image[8190]=8'he0;bios_image[8191]=0;
        power_cycle();
        check(core_id==32'h56454358 && !ready && !bios_loaded && !cart_loaded,
            "Power up has no embedded ROM or ready state");
        check(input_ready && load_ready,"Transport ready after reset");
        load_bios();
        check(bios_loaded && !cart_loaded && ready,"BIOS alone makes native core ready");
        check(dut.bios[0]==8'h86 && dut.bios[8191]==0,"BIOS first and last byte stored");
        cart(65536,0);
        check(cart_loaded && dut.cart_size==65536 && ready,"64 KiB cart accepted");
        check(dut.cartridge[0]==8'haa && dut.cartridge[32767]==8'haa &&
            dut.cartridge[32768]==8'h55 && dut.cartridge[65535]==8'h55,
            "64 KiB boundary bytes stored");
        @(negedge clk);pause_request=0;
        n=0;
        while((dut.ram[0]!==8'haa || dut.ram[1]!==8'h55) && n<6000) begin
            @(negedge clk);n=n+1;
        end
        check(dut.ram[0]==8'haa && dut.ram[1]==8'h55,"6809 VIA PB6 selects both cartridge banks");
        if(dut.ram[0]!==8'haa || dut.ram[1]!==8'h55)
            $display("BANK DEBUG RAM=%h/%h PC=%h PB=%h DDR=%h READY=%b RESET=%b",dut.ram[0],dut.ram[1],
                debug_cpu_address,dut.pb_bus,dut.pb_ddr,ready,dut.cpu_reset);
        @(negedge clk);pause_request=1;
        repeat(40) @(negedge clk);
        paused_pc=debug_cpu_address;
        check(dut.cpu_paused,"CPU reaches safe pause boundary");
        repeat(100) begin
            @(negedge clk);
            check(!debug_cpu_read && !debug_cpu_write && debug_cpu_address==paused_pc,
                "Paused CPU emits no bus transfer");
        end
        repeat(510) @(negedge clk);
        check(audio_l==0 && audio_r==0,"Paused core audio is silent");
        @(negedge clk);core_reset_request=1;
        repeat(4) @(negedge clk);
        check(bios_loaded && cart_loaded,"Core reset preserves loaded content");
        mode(2,0);
        check(!cart_loaded && dut.cart_size==0 && !dut.gs_mode,"Mode 2 ejects cartridge under reset");
        @(negedge clk);core_reset_request=0;
        repeat(WIDTH*HEIGHT+8) @(negedge clk);
        check(ready && bios_loaded,"Ejection leaves BIOS-only native core ready");
        cart(32769,0);
        check(cart_loaded && dut.cart_size==32769,"Non-power-of-two cartridge length retained");
        force dut.cpu_address=16'h0001;
        force dut.pb_bus=8'h40;
        repeat(4) @(negedge clk);
        check(dut.cart_address==16'h8001 && dut.cpu_data_in==8'hff,
            "Bytes beyond odd-sized cartridge return open bus");
        release dut.cpu_address;release dut.pb_bus;

        expect_bad(8'h40,8191,0,1,0,0,"Reject incorrect BIOS size and invalidate BIOS");
        check(!bios_loaded,"Malformed BIOS first invalidates previous BIOS");
        expect_bad(8'h41,0,0,1,1,0,"Reject zero-length cartridge");
        expect_bad(8'h41,65537,0,1,0,0,"Reject oversized cartridge");
        expect_bad(8'h41,2,0,0,0,0,"Reject missing first marker");
        expect_bad(8'h41,2,1,1,1,0,"Reject nonzero starting offset");
        expect_bad(8'h41,2,0,1,1,0,"Reject premature last marker");
        expect_bad(8'h41,1,0,1,0,0,"Reject absent final last marker");
        expect_bad(8'h99,1,0,1,1,0,"Reject unknown load kind");
        expect_bad(8'h44,1,0,1,1,3,"Reject unsupported mode");
        expect_bad(8'h42,9,0,1,0,0,"Reject ARM lines outside BASIC mode");
        expect_bad(8'h43,2,0,1,0,0,"Reject ARM PSG outside BASIC mode");
        clear_error();
        send_byte(8'h41,3,0,1,0,0,8'h71);
        send_byte(8'h41,3,2,0,1,0,8'h72);
        check(load_error && !cart_loaded,"Reject skipped offset");
        clear_error();
        send_byte(8'h41,3,0,1,0,0,8'h71);
        send_byte(8'h41,3,0,0,0,0,8'h72);
        check(load_error && !cart_loaded,"Reject duplicate offset");
        clear_error();
        send_byte(8'h41,3,0,1,0,0,8'h71);
        send_byte(8'h41,4,1,0,0,0,8'h72);
        check(load_error,"Reject changed total mid-object");
        clear_error();
        send_byte(8'h41,3,0,1,0,0,8'h71);
        send_byte(8'h40,3,1,0,0,0,8'h72);
        check(load_error,"Reject changed kind mid-object");
        clear_error();
        send_byte(8'h41,3,0,1,0,0,8'h71);
        send_byte(8'h41,3,1,1,0,0,8'h72);
        check(load_error,"Reject repeated first in same epoch");

        power_cycle();
        mode(1,0);
        check(ready && dut.gs_mode && !bios_loaded,"BASIC mode is ready without BIOS");
        input_event(16'h2000,16'hffa5);
        input_event(16'h2001,16'h8000);input_event(16'h2002,16'h7fff);
        input_event(16'h2003,16'hff00);input_event(16'h2004,16'h0100);
        check(dut.buttons==8'ha5 && dut.psg_i.IOA_in==8'h5a,"Eight active-high buttons reach active-low AY port");
        check(dut.joy_x1==-128 && dut.joy_y1==127 && dut.joy_x2==-1 && dut.joy_y2==1,
            "Signed controller axes use signed high byte");
        input_event(16'h1000,0);input_event(16'h2005,0);
        check(dut.buttons==8'ha5 && dut.joy_x1==-128,"Unrelated namespace and input index ignored");
        @(negedge clk);core_reset_request=1;
        input_event(16'h2000,0);input_event(16'h2001,0);input_event(16'h2002,0);
        input_event(16'h2003,0);input_event(16'h2004,0);
        check(input_ready && dut.buttons==8'ha5 && dut.joy_x1==-128 && dut.joy_y1==127 &&
            dut.joy_x2==-1 && dut.joy_y2==1,"Core reset drains queued input without applying it");
        @(negedge clk);core_reset_request=0;
        repeat(WIDTH*HEIGHT+8) @(negedge clk);
        input_event(16'h2000,16'h003c);input_event(16'h2001,16'h1200);input_event(16'h2002,16'hee00);
        input_event(16'h2003,16'h3400);input_event(16'h2004,16'hcc00);
        check(dut.buttons==8'h3c && dut.joy_x1==18 && dut.joy_y1==-18 && dut.joy_x2==52 && dut.joy_y2==-52,
            "Fresh held-control reassertion applies after reset release");
        before_lines=captured_lines;
        arm_line(-16384,-8192,16384,8192,8'hb7,0);
        repeat(4) @(negedge clk);
        check(dut.arm_x0==-16384 && dut.arm_y0==-8192 && dut.arm_x1==16384 &&
            dut.arm_y1==8192 && dut.arm_intensity==8'hb7,"Little-endian signed line decoded");
        check(captured_lines==before_lines+1 && captured_line=={16'd1,16'd28,16'd30,16'd13,8'hb7},
            "Native centered signed coordinates project into screen pixels once");
        // Pause stalls rendering, not command acceptance. Fill the 64-entry
        // queue and one pending line, then resume and prove lossless drain.
        for(n=0;n<64;n=n+1) arm_line(0,0,0,0,8'h30,0);
        check(!load_ready && dut.line_pending && dut.renderer_i.queued==64,
            "Full renderer queue backpressures additional load packets");
        input_event(16'h2000,16'h005a);
        check(dut.buttons==8'h5a,"Input remains independent of blocked line queue");
        @(negedge clk);pause_request=0;
        n=0;
        while(!load_ready && n<5000) begin @(negedge clk);n=n+1;end
        check(load_ready && captured_lines==before_lines+65 && overflow_count==0,
            "Resuming drains pending line once without queue overflow");
        @(negedge clk);pause_request=1;
        before_lines=captured_lines;
        send_byte(8'h42,9,0,1,0,0,8'hee);
        send_byte(8'h42,9,2,0,0,0,8'hee);
        check(load_error && captured_lines==before_lines,
            "Malformed partial line cannot commit renderer coordinates");
        clear_error();
        psg_write(8,15);
        check(dut.psg_i.ymreg[8]==15,"Two-byte PSG command writes selected AY register");
        psg_write(16,1);
        check(load_error,"Reject PSG register outside 0..15");
        clear_error();
        expect_bad(8'h42,8,0,1,0,0,"Reject wrong line packet size");
        expect_bad(8'h43,3,0,1,0,0,"Reject wrong PSG packet size");
        clear_error();
        mode(0,0);
        check(!ready && !dut.gs_mode,"Return to native without BIOS is not ready");

        // Independent abort mailbox can arrive before, during, or after bytes.
        power_cycle();
        send_byte(8'h40,8192,0,1,0,0,8'h11);
        abort_transfer(0);
        send_byte(8'h40,8192,8191,0,1,0,8'hee);
        check(!bios_loaded && !ready && !load_error,"Aborted BIOS cannot become ready on stale last byte");
        power_cycle();
        send_byte(8'h41,3,0,1,0,0,8'h11);
        abort_transfer(0);
        send_byte(8'h41,3,1,0,0,0,8'hee);
        send_byte(8'h41,3,2,0,1,0,8'hee);
        check(!dut.stream_active && !cart_loaded && !load_error,
            "Abort drops queued stale bytes without committing cartridge");
        cart(1,1);
        check(cart_loaded && !load_error && dut.cartridge[0]==8'haa,
            "New epoch succeeds after stale FIFO bytes drain");
        power_cycle();
        abort_transfer(0);
        send_byte(8'h41,1,0,1,1,0,8'hee);
        check(!cart_loaded && !load_error,"Abort arriving before first byte rejects entire stale epoch");
        cart(1,1);
        check(cart_loaded && !load_error,"New epoch starts after early abort");
        power_cycle();
        send_byte(8'h41,3,0,1,0,0,8'h11);
        send_byte(8'h41,2,0,1,0,1,8'h22);
        abort_transfer(0);
        send_byte(8'h41,2,1,0,1,1,8'h33);
        check(cart_loaded && !load_error && dut.cartridge[0]==8'h22 && dut.cartridge[1]==8'h33,
            "Delayed old-epoch abort does not cancel new active transfer");
        power_cycle();
        send_byte(8'h41,2,0,1,0,0,8'h11);
        @(negedge clk);load_abort=1;load_abort_epoch=0;
        load_kind=8'h41;load_total=2;load_offset=1;load_first=0;load_last=1;
        load_epoch=0;load_data=8'hee;load_valid=1;
        @(negedge clk);load_valid=0;load_abort=0;
        check(!cart_loaded && !dut.stream_active && !load_error,
            "Abort coinciding with last byte wins over commit");
        cart(1,1);
        check(cart_loaded && !load_error,"Transfer recovers after simultaneous abort");
        // Both epoch values must be reusable after their previous abort drains.
        abort_transfer(1);
        cart(1,0);
        check(cart_loaded && !load_error,"One-bit epoch wraps after drained abort");
        power_cycle();
        mode(1,0);
        for(n=0;n<65;n=n+1) arm_line(0,0,0,0,8'h30,0);
        check(!load_ready && dut.line_pending,"Reset test has a pending line behind full queue");
        @(negedge clk);core_reset_request=1;
        repeat(5) @(negedge clk);
        check(load_ready && !dut.line_pending && !dut.psg_pending,
            "Core reset discards pending ARM commands and releases transport");
        if(load_ready) begin
            mode(2,1);
            check(!dut.gs_mode && !load_error,"Mode load remains available while core reset is held");
            load_bios();
            check(bios_loaded && !load_error,"Full BIOS transfer completes while core reset is held");
            @(negedge clk);core_reset_request=0;
            repeat(WIDTH*HEIGHT+8) @(negedge clk);
            mode(1,0);
            send_byte(8'h43,2,0,1,0,0,8'd8);
            send_byte(8'h43,2,1,0,1,0,8'd15);
            check(dut.psg_pending,"Reset test catches pending PSG transaction");
            core_reset_request=1;
            repeat(5) @(negedge clk);
            check(!dut.psg_pending && dut.psg_phase==0 && load_ready,
                "Core reset cancels pending PSG phase without blocking transport");
        end
        power_cycle();
        check(!bios_loaded && !cart_loaded && !dut.gs_mode && dut.buttons==0 && dut.joy_x1==0,
            "Power reset clears mode, content validity, and controller state");
        if(failures) $fatal(1,"Vectrex transport: %0d of %0d checks failed",failures,checks);
        $display("PASS: Vectrex transport %0d checks",checks);
        $finish;
    end
    initial begin #10000000;$fatal(1,"Vectrex transport timeout");end
endmodule

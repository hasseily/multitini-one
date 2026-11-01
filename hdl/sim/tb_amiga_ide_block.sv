// Original native IDE block fixture. SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_amiga_ide_block;
    reg clk=0,control_clk=0,reset=1,control_resetn=0,guest_reset=0;
    always #17.621 clk=~clk;
    always #3.759 control_clk=~control_clk;
    reg native_running=1,native_enable=1;
    always @(negedge clk)native_enable<=native_running;
    wire chip_tick=!reset && native_enable;
    wire native_clk;
    assign #0.2 native_clk=clk && native_enable;
    reg write=0,block_write=0,block_read=0;
    reg [31:0] command=0;
    reg [15:0] block_address=0,block_read_address=0;
    reg [6:0] block_ram_read_address=0;
    reg [3:0] block_wstrb=15;
    reg [15:0] native_config=16'h066b;
    wire [31:0] status,block_rdata;
    wire [4:0] ide_address;
    wire ide_read,ide_write;
    wire [15:0] ide_wdata,ide_rdata;
    wire [2:0] native_request;
    wire [4:0] delayed_address;
    wire [15:0] delayed_wdata;
    wire delayed_read,delayed_write;
    assign #1 delayed_address=ide_address;
    assign #1 delayed_wdata=ide_wdata;
    assign #1 delayed_read=ide_read;
    assign #1 delayed_write=ide_write;
    reg [3:0] guest_address=0;
    reg guest_read=0,guest_write=0;
    reg [31:0] guest_wdata=0;
    wire [31:0] guest_rdata;
    multitini_amiga_host #(.BLOCK_STALL_TIMEOUT_CYCLES(128)) host_i(
        .control_clk(control_clk),.control_resetn(control_resetn),.write(write),.command(command),.status(status),
        .block_write(block_write),.block_read(block_read),.block_address(block_address),
        .block_read_address(block_read_address),.block_ram_read_address(block_ram_read_address),
        .block_wstrb(block_wstrb),.block_rdata(block_rdata),
        .clk(clk),.reset(reset),.guest_reset(guest_reset),.native_config(native_config),.chip_tick(chip_tick),
        .io_fpga(),.io_uio(),.io_strobe(),.io_din(),.io_wait(1'b0),.io_dout(16'd0),
        .ide_req({3'd0,native_request}),.ide_address(ide_address),.ide_read(ide_read),.ide_write(ide_write),
        .ide_wdata(ide_wdata),.ide_rdata(ide_rdata));
    ide native_i(.clk(native_clk),.rst_n(!reset && !guest_reset),.irq(),.drq(),.use_fast(1'b1),.no_data(),
        .drive_en(),.io_address(guest_address),.io_read(guest_read),.io_readdata(guest_rdata),.io_write(guest_write),
        .io_writedata(guest_wdata),.io_32(1'b0),.io_wait(),.request(native_request),
        .mgmt_address(delayed_address[3:0]),.mgmt_write(delayed_write),.mgmt_writedata(delayed_wdata),
        .mgmt_read(delayed_read),.mgmt_readdata(ide_rdata));
    integer commands=0,completions=0,native_pulses=0,held_edges=0,service_cycles=0,ram_writes=0;
    reg strobe_old=0,auto_pause=0;
    always @(posedge control_clk)begin
        if(control_resetn && block_write && $isunknown(command))
            $fatal(1,"unknown command before staging write");
        if(control_resetn && host_i.block_i.command_pending && host_i.block_i.command_ready)commands<=commands+1;
        if(control_resetn && host_i.block_i.response_valid)completions<=completions+1;
    end
    always @(posedge native_clk)begin
        if(!reset && (delayed_read || delayed_write) && !strobe_old)native_pulses<=native_pulses+1;
        strobe_old<=delayed_read || delayed_write;
    end
    always @(posedge clk)begin
        service_cycles<=service_cycles+1;
        if(host_i.block_i.ram_native_write)ram_writes<=ram_writes+1;
        if(!native_enable)held_edges<=held_edges+1;
        if(auto_pause)begin
            if(service_cycles%37==0)native_running<=0;
            if(service_cycles%37==5)native_running<=1;
        end
    end
    task tick;begin @(posedge clk);#2;end endtask
    task cw(input [15:0] address,input [31:0] value,input [3:0] strobes);
        begin
            if($isunknown(value))$fatal(1,"unknown block command argument before driver assignment");
            @(negedge control_clk);block_address=address;command=value;block_wstrb=strobes;block_write=1;
            @(negedge control_clk);block_write=0;
        end
    endtask
    task cr(input [15:0] address,output [31:0] value);
        begin
            @(negedge control_clk);block_read_address=address;block_ram_read_address=address[8:2];block_read=1;
            @(posedge control_clk);#1;value=block_rdata;
            @(negedge control_clk);block_read=0;
        end
    endtask
    task legacy(input [7:0] opcode,input [4:0] address,input [15:0] value,input bit allow_error);
        integer n;reg old_toggle;
        begin
            old_toggle=status[29];
            @(negedge control_clk);command={opcode,3'd0,address,value};write=1;
            @(negedge control_clk);write=0;
            n=0;while(status[29]==old_toggle && n<20000)begin tick;n=n+1;end
            if(n==20000 || (!allow_error && status[30]))$fatal(1,"legacy IDE operation failed:%h",status);
            repeat(20)tick;
        end
    endtask
    function automatic [15:0] pattern(input integer n,input integer salt);
        pattern=(16'ha55a ^ (n*16'h0103))+(salt*16'h1739);
    endfunction
    task fill(input integer salt);
        integer n;reg [15:0] low_word,high_word;
        begin
            for(n=0;n<128;n=n+1)begin
                // Separate values survive the delayed task call identically
                // in Icarus and XSim; nested function-concat task arguments
                // can lose their low temporary in the latter simulator.
                low_word=pattern(2*n,salt);high_word=pattern(2*n+1,salt);
                cw(16'hd200+n*4,{high_word,low_word},15);
            end
        end
    endtask
    task start_block(input [15:0] seq_id,input bit direction);
        begin cw(16'hd104,{seq_id,15'd256,direction},15);end
    endtask
    task finish_block(input [15:0] seq_id,input [4:0] expected_error,input integer expected_words);
        integer n;reg [31:0] value,progress;
        begin
            n=0;value=0;
            while(n<20000 && !(value[29] && !value[31] && value[15:0]==seq_id))begin cr(16'hd100,value);n=n+1;end
            cr(16'hd108,progress);
            if(n==20000 || value[30]!=(expected_error!=0) || value[28:24]!=expected_error || progress!=expected_words)
                $fatal(1,"block seq%h expectederr%0d words%0d gotstatus%h progress%0d",seq_id,expected_error,expected_words,value,progress);
        end
    endtask
    task guest_write_word(input [15:0] value);
        begin
            @(negedge native_clk);guest_address=0;guest_wdata={16'd0,value};guest_write=1;
            @(posedge native_clk);#2;
            @(negedge native_clk);guest_write=0;
            repeat(3)@(posedge native_clk);
        end
    endtask
    task guest_check_sector(input integer salt);
        integer n;reg [15:0] value;
        begin
            legacy(3,0,1,0);legacy(3,5,16'h5e00,0);
            for(n=0;n<256;n=n+1)begin
                @(negedge native_clk);guest_address=0;guest_read=1;
                @(posedge native_clk);#2;value=guest_rdata[15:0];
                if(value!==pattern(n,salt))$fatal(1,"guest PIO word%0d expected%h got%h",n,pattern(n,salt),value);
                @(negedge native_clk);guest_read=0;
                repeat(3)@(posedge native_clk);
            end
        end
    endtask
    task stage_check_sector(input integer salt);
        integer n;reg [31:0] value,want;reg [15:0] low_word,high_word;
        begin
            for(n=0;n<128;n=n+1)begin
                cr(16'hd200+n*4,value);
                low_word=pattern(2*n,salt);high_word=pattern(2*n+1,salt);want={high_word,low_word};
                if(value!==want)$fatal(1,"ARM stage word%0d expected%h got%h",n,want,value);
            end
        end
    endtask
    task pl_reset;
        begin
            auto_pause=0;native_running=1;guest_reset=0;
            @(negedge clk);reset=1;control_resetn=0;
            repeat(12)tick;@(negedge clk);reset=0;control_resetn=1;
            repeat(80)tick;strobe_old=0;
        end
    endtask
    integer old_commands,old_completions,old_pulses,n;
    reg [31:0] value;
    initial begin
        pl_reset();
        if(!status[25] || !status[24])$fatal(1,"bulk/CPU14 capability absent");
        legacy(11,0,0,0);
        if(status[15:0]!=16'h066b)$fatal(1,"native CPU/memory configuration readback incorrect");
        native_config=16'h0443;legacy(11,0,0,0);
        if(status[15:0]!=16'h0443)$fatal(1,"native configuration readback was stale");
        legacy(11,0,1,1);
        if(!status[30])$fatal(1,"READ_CONFIG reserved payload accepted");
        legacy(4,0,0,0);
        // Invalid framing and incomplete staging must issue no native strobe.
        old_pulses=native_pulses;
        cw(16'hd104,{16'd1,15'd255,1'b0},15);finish_block(1,1,0);
        cw(16'hd104,{16'd0,15'd256,1'b0},15);finish_block(0,1,0);
        cw(16'hd104,{16'd2,15'd256,1'b0},3);finish_block(2,1,0);
        start_block(3,0);finish_block(3,6,0);
        if(native_pulses!=old_pulses)$fatal(1,"invalid block touched native IDE");
        legacy(3,6,9,0); // Native drive0 presence; normal legacy path remains available.
        fill(1);stage_check_sector(1);auto_pause=1;
        old_commands=commands;old_completions=completions;old_pulses=native_pulses;
        start_block(4,0);finish_block(4,0,256);
        if(commands!=old_commands+1 || completions!=old_completions+1 || native_pulses!=old_pulses+257)
            $fatal(1,"sector transfer was not exactly1descriptor/1completion/257nativepulses");
        auto_pause=0;native_running=1;repeat(5)tick;
        guest_check_sector(1);
        // Guest PIO fills the native FIFO. One bulk read must reproduce all
        // 512bytes, independently of the management-side write mover.
        legacy(3,0,1,0);legacy(3,5,16'h5800,0);
        for(n=0;n<256;n=n+1)guest_write_word(pattern(n,2));
        auto_pause=1;start_block(5,1);finish_block(5,0,256);
        auto_pause=0;native_running=1;stage_check_sector(2);
        // RAM and descriptor writes while owned are rejected, not allowed
        // to corrupt the accepted block or replace its seq_id.
        fill(3);start_block(6,0);
        wait(host_i.block_i.native_owned);
        cw(16'hd200,32'hdeadbeef,15);start_block(99,1);
        finish_block(6,2,256);guest_check_sector(3);
        // A legacy command while the mover owns IDE must not pulse it.
        fill(4);start_block(7,0);wait(host_i.block_i.native_owned);
        @(negedge control_clk);command={8'd3,3'd0,5'd15,16'hdead};write=1;
        @(negedge control_clk);write=0;
        finish_block(7,0,256);
        if(!status[30])$fatal(1,"legacy collision was not rejected");
        legacy(4,0,0,0);guest_check_sector(4);
        // The descriptor cannot steal a live legacy transaction.
        native_running=0;repeat(5)tick;
        @(negedge control_clk);command={8'd2,3'd0,5'd1,16'd0};write=1;
        @(negedge control_clk);write=0;
        start_block(8,1);finish_block(8,5,0);
        native_running=1;repeat(100)tick;
        // Stop after a known partial block. Timeout must remain poisoned,
        // including after legacy CLEAR_ERROR and a new bulk doorbell.
        fill(5);start_block(9,0);
        wait(host_i.block_i.words>=7);@(negedge clk);native_running=0;
        repeat(180)tick;
        cr(16'hd100,value);
        if(value[31] || !value[30] || !value[29] || value[28:24]!=3 || value[15:0]!=9)
            $fatal(1,"native stall did not terminate/poison:%h",value);
        old_commands=commands;old_pulses=native_pulses;
        native_running=1;repeat(10)tick;legacy(4,0,0,0);
        fill(6);start_block(10,0);repeat(50)tick;cr(16'hd100,value);
        if(value[28:24]!=3 || value[15:0]!=9 || commands!=old_commands || native_pulses!=old_pulses)
            $fatal(1,"poisoned partial block was retried");
        pl_reset();legacy(3,6,9,0);fill(7);start_block(11,0);finish_block(11,0,256);guest_check_sector(7);
        // Guest reset aborts but cannot recycle an ownership epoch. Only a
        // full PL reset clears the poison; no old descriptor may replay.
        fill(8);start_block(12,0);wait(host_i.block_i.words>=9);
        @(negedge clk);guest_reset=1;repeat(40)tick;
        cr(16'hd100,value);
        if(value[28:24]!=4 || value[15:0]!=12 || value[31])$fatal(1,"guest reset abort missing:%h",value);
        guest_reset=0;old_commands=commands;start_block(13,1);repeat(40)tick;
        if(commands!=old_commands)$fatal(1,"guest reset recycled poisoned block");
        pl_reset();old_pulses=native_pulses;repeat(100)tick;
        if(native_pulses!=old_pulses)$fatal(1,"stale block replayed after PL reset");
        // Reset only the control endpoint during a live native-to-ARM block.
        // The service domain retains an epoch poison; old work may neither
        // overwrite newly owned RAM nor accept a replacement descriptor.
        legacy(3,6,9,0);fill(9);start_block(14,0);finish_block(14,0,256);
        start_block(15,1);wait(host_i.block_i.words>=7);
        @(negedge control_clk);control_resetn=0;
        repeat(16)tick;@(negedge control_clk);control_resetn=1;repeat(100)tick;
        cr(16'hd100,value);
        if(value[31] || !value[30] || !value[29] || value[28:24]!=4)
            $fatal(1,"independent control reset lost epoch poison:%h",value);
        old_commands=commands;old_pulses=native_pulses;n=ram_writes;
        fill(10);start_block(16,0);repeat(60)tick;
        if(commands!=old_commands || native_pulses!=old_pulses || ram_writes!=n)
            $fatal(1,"independent control reset allowed late RAM writes or replay");
        pl_reset();legacy(3,6,9,0);fill(11);start_block(17,0);finish_block(17,0,256);guest_check_sector(11);
        // A full PL reset may interrupt a block, but must drain its command
        // and completion handshakes before publishing a fresh ownership.
        fill(12);start_block(18,0);wait(host_i.block_i.words>=5);pl_reset();
        old_pulses=native_pulses;repeat(100)tick;
        if(native_pulses!=old_pulses)$fatal(1,"pre-reset partial descriptor replayed");
        legacy(3,6,9,0);fill(13);start_block(19,0);finish_block(19,0,256);guest_check_sector(13);
        if(held_edges<50)$fatal(1,"native pause coverage absent");
        $display("PASS AMIGA BULK IDE nativePIO/endian/256words/1descriptor/ownership/busy/invalid/timeout/poison/reset/legacy capabilities held%0d",held_edges);
        $finish;
    end
    initial begin #20000000;$fatal(1,"bulk IDE watchdog");end
endmodule

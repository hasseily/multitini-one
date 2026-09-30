// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// End-to-end board adapter: original 68k program, actual byte loader/cache,
// delayed memory replies, SRAM restore, coherent snapshot and failed retry.
module tb_genesis_adapter;
    reg core_clk=0,reset=1,core_reset_request=1,clear_load_error=0;
    always #5 core_clk=~core_clk;
    reg [7:0] load_data=0,load_kind=0;
    reg load_valid=0,load_first=0,load_last=0,load_epoch=0;
    reg [31:0] load_offset=0,load_total=0;
    wire load_ready;
    wire [23:0] memory_addr;
    wire memory_write,memory_valid,memory_ready;
    wire [63:0] memory_wdata;
    wire [7:0] memory_wstrb;
    reg [63:0] memory_rdata=0;
    reg memory_rvalid=0;
    reg [63:0] memory[0:127];
    reg [7:0] rom[0:1023];
    reg [7:0] config_bytes[0:15];
    reg media_mount=0,media_eject=0,media_complete=0;
    reg [31:0] media_generation=7,media_complete_request_id=0,media_complete_status=0;
    reg [31:0] media_complete_generation=7;
    reg [15:0] media_complete_block_count=256;
    wire media_request_valid,media_request_write;
    wire [31:0] media_request_id,media_request_lba,media_request_generation;
    wire [15:0] media_request_block_count;
    wire [7:0] media_write_data;
    wire media_write_data_valid,media_write_data_last,media_write_data_ready;
    wire media_state_mounted,media_state_dirty,media_state_eject_pending;
    wire [31:0] core_id,cart_size;
    wire ready,load_error,cart_loaded;
    integer ticks=0,delay_count=0,read_count=0,write_count=0,received=0,snapshots=0;
    reg compare_snapshot=0;
    assign memory_ready=delay_count==0 && !memory_rvalid && ticks%3!=0;
    assign media_write_data_ready=compare_snapshot && ticks%5!=0;
    multitini_genesis_adapter dut(
        .core_clk(core_clk),.reset(reset),.core_reset_request(core_reset_request),.pause_request(1'b0),.clear_load_error(clear_load_error),
        .input_code(16'd0),.input_value(16'd0),.input_valid(1'b0),.input_ready(),
        .load_data(load_data),.load_kind(load_kind),.load_valid(load_valid),.load_ready(load_ready),
        .load_first(load_first),.load_last(load_last),.load_offset(load_offset),.load_total(load_total),
        .load_epoch(load_epoch),.load_abort(1'b0),.load_abort_epoch(1'b0),
        .memory_addr(memory_addr),.memory_write(memory_write),.memory_wdata(memory_wdata),.memory_wstrb(memory_wstrb),
        .memory_valid(memory_valid),.memory_ready(memory_ready),.memory_rdata(memory_rdata),.memory_rvalid(memory_rvalid),.memory_fault(1'b0),
        .media_mount(media_mount),.media_eject(media_eject),.media_complete(media_complete),
        .media_slot(8'd0),.media_kind(8'h31),.media_read_only(1'b0),.media_image_size(32'd65536),.media_generation(media_generation),
        .media_complete_request_id(media_complete_request_id),.media_complete_status(media_complete_status),
        .media_complete_block_count(media_complete_block_count),.media_complete_generation(media_complete_generation),
        .media_request_valid(media_request_valid),.media_request_write(media_request_write),.media_request_id(media_request_id),
        .media_request_lba(media_request_lba),.media_request_block_count(media_request_block_count),.media_request_generation(media_request_generation),
        .media_write_data(media_write_data),.media_write_data_valid(media_write_data_valid),.media_write_data_last(media_write_data_last),
        .media_write_data_ready(media_write_data_ready),.media_state_mounted(media_state_mounted),.media_state_dirty(media_state_dirty),
        .media_state_eject_pending(media_state_eject_pending),.core_id(core_id),.ready(ready),.load_error(load_error),
        .cart_loaded(cart_loaded),.cart_size(cart_size),.video_r(),.video_g(),.video_b(),.video_de(),.video_hsync(),.video_vsync(),
        .video_valid(),.video_mode_240(),.audio_l(),.audio_r(),.audio_valid());
    function automatic [7:0] save_pattern(input integer address);
        save_pattern=8'((address*37)^(address>>8));
    endfunction
    integer lane;
    always @(posedge core_clk)begin
        ticks<=ticks+1;memory_rvalid<=0;
        if(delay_count>0)begin
            delay_count<=delay_count-1;
            if(delay_count==1)memory_rvalid<=1;
        end
        if(memory_valid && memory_ready)begin
            if(memory_addr>=1024 || memory_addr[2:0]!=0)$fatal(1,"Adapter ROM request escaped cartridge: %h",memory_addr);
            delay_count<=19+(ticks%13);
            if(memory_write)begin
                for(lane=0;lane<8;lane=lane+1)if(memory_wstrb[lane])memory[memory_addr>>3][lane*8+:8]<=memory_wdata[lane*8+:8];
                write_count<=write_count+1;
            end else begin memory_rdata<=memory[memory_addr>>3];read_count<=read_count+1;end
        end
        if(media_write_data_valid && media_write_data_ready)begin
            if(media_write_data!==(received==0 ? 8'h5a : save_pattern(received)))
                $fatal(1,"Adapter snapshot byte%0d got%h expected%h",received,media_write_data,received==0 ? 8'h5a : save_pattern(received));
            if(media_write_data_last!=(received==65535))$fatal(1,"Adapter snapshot LAST misplaced");
            received<=received+1;
        end
    end
    task send_byte(input [7:0] kind,input integer offset,total,input [7:0] value);
        begin
            @(negedge core_clk);load_data=value;load_kind=kind;load_offset=offset;load_total=total;
            load_first=offset==0;load_last=offset==total-1;load_valid=1;
            do @(posedge core_clk);while(!load_ready);
            @(negedge core_clk);load_valid=0;
        end
    endtask
    task put_word(input integer address,input [15:0] value);
        begin rom[address]=value[15:8];rom[address+1]=value[7:0];end
    endtask
    task snapshot(input bit fail_reply);
        begin
            wait(media_request_valid);
            if(!media_request_write || media_request_lba!=0 || media_request_block_count!=256 || media_request_generation!=7)
                $fatal(1,"Adapter snapshot descriptor mismatch");
            @(negedge core_clk);received=0;compare_snapshot=1;
            wait(received==65536);@(negedge core_clk);compare_snapshot=0;
            repeat(4)@(negedge core_clk);
            media_complete_request_id=media_request_id;media_complete_status=fail_reply ? 1 : 0;media_complete=1;
            @(negedge core_clk);media_complete=0;repeat(5)@(negedge core_clk);
            snapshots=snapshots+1;
        end
    endtask
    integer i;
    initial begin
        for(i=0;i<1024;i=i+1)rom[i]=8'hff;
        for(i=0;i<16;i=i+1)config_bytes[i]=0;
        config_bytes[0]="M";config_bytes[1]="D";config_bytes[2]="0";config_bytes[3]="1";
        config_bytes[4]=1;config_bytes[5]=3;config_bytes[7]=1;
        put_word(0,16'h00ff);put_word(2,16'hff00);put_word(4,0);put_word(6,16'h0200);
        put_word(256,16'h5345);put_word(258,16'h4741);
        put_word(512,16'h46fc);put_word(514,16'h2700);
        put_word(516,16'h33fc);put_word(518,16'ha55a);put_word(520,16'h00ff);put_word(522,16'h0010);
        put_word(524,16'h33fc);put_word(526,16'h005a);put_word(528,16'h0020);put_word(530,0);put_word(532,16'h60fe);
        repeat(8)@(negedge core_clk);reset=0;
        for(i=0;i<16;i=i+1)send_byte(8'h32,i,16,config_bytes[i]);
        load_epoch=1;
        for(i=0;i<1024;i=i+1)send_byte(8'h30,i,1024,rom[i]);
        wait(cart_loaded);
        load_epoch=0;
        for(i=0;i<65536;i=i+1)send_byte(8'h31,i,65536,save_pattern(i));
        repeat(3)@(negedge core_clk);media_mount=1;@(negedge core_clk);media_mount=0;
        core_reset_request=0;wait(ready);wait(media_state_dirty);
        if(dut.machine_i.ram68k_i.memory[15'h2008]!==16'ha55a)$fatal(1,"Full adapter 68000 signature missing");
        if(core_id!=32'h4d442020 || cart_size!=1024 || write_count!=128 || read_count==0 || load_error)
            $fatal(1,"Full adapter boot/transport status");
        @(negedge core_clk);media_generation=8;media_eject=1;@(negedge core_clk);media_eject=0;
        snapshot(1);
        if(!load_error || !media_state_dirty || !media_state_eject_pending || !dut.save_pause)$fatal(1,"Failed snapshot lost hold/dirty state");
        @(negedge core_clk);clear_load_error=1;@(negedge core_clk);clear_load_error=0;
        snapshot(0);
        if(load_error || media_state_mounted || media_state_dirty || !dut.save_pause)$fatal(1,"Successful snapshot/eject barrier mismatch");
        repeat(30)@(negedge core_clk);
        if(!dut.save_pause)$fatal(1,"Eject resumed before remount");
        media_mount=1;@(negedge core_clk);media_mount=0;repeat(3)@(negedge core_clk);
        if(!media_state_mounted || dut.save_pause)$fatal(1,"Remount failed to release chip");
        $display("PASS full Genesis adapter: CONFIG/ROM/SAVE, real68000, %0d delayedcache reads, %0d exact64KiB snapshots, failure/retry/eject",read_count,snapshots);
        $finish;
    end
    initial begin #30000000;$fatal(1,"Adapter timeout ready%0b dirty%0b received%0d",ready,media_state_dirty,received);end
endmodule

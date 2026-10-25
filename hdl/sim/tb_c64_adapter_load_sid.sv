// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Full adapter: actual T65/island/proxy, UCI, VIC/CIA, REU and REU cache.
// Only ROM contents, host UCI buffer/operations and DDR responses are fixtures.
module tb_c64_adapter_load_sid #(parameter integer NTSC=0);
    reg clk=0,fastclk=0;
    initial forever begin
        #7.8125;clk=1;fastclk=1;
        #7.8125;fastclk=0;
        #7.8125;clk=0;fastclk=1;
        #7.8125;fastclk=0;
    end
    reg reset=1,pause_request=1;
    reg [31:0] config_word=0;
    reg [10:0] control=11'h31f;
    reg [31:0] op=0;
    reg op_valid=0;
    wire [31:0] uci_status,uci_status2,cpu_status;
    wire [10:0] buffer_addr;
    wire buffer_we;
    wire [7:0] buffer_wdata;
    reg [7:0] buffer_rdata=0,buffer[0:2047],rom[0:8191];
    reg [7:0] load_data=0,load_kind=0;
    reg load_valid=0,load_first=0,load_last=0;
    reg [31:0] load_offset=0,load_total=0;
    wire load_ready,load_error,ready;
    wire [23:0] ddr_addr;
    wire ddr_write,ddr_valid,ddr_burst,ddr_busy;
    wire [63:0] ddr_wdata;
    wire [7:0] ddr_wstrb;
    reg [63:0] ddr_rdata=0;
    reg ddr_rvalid=0,pending=0;
    reg [23:0] pending_addr;
    reg pending_write;
    reg [63:0] pending_data;
    reg [7:0] pending_strb;
    reg [7:0] backing[0:511];
    integer delay_count=0,beats_left=0,beat_index=0,requests=0;
    wire ddr_ready=!pending&&!reset;
    reg map_enable=0,reu_enable=0,ua_enable=0,uci_enable=0;
    reg [127:0] io_masks=0;
    wire [23:0] cart_addr;wire cart_write,cart_valid;
    wire [63:0] cart_wdata;wire [7:0] cart_wstrb;
    reg cart_rvalid=0;reg [7:0] cart_backing[0:8191];
    reg cart_pending=0;integer cart_delay=0,cart_bytes=0,uci_bytes=0;
    wire cart_ready=!cart_pending;
    multitini_c64_adapter #(.NTSC_MODE(NTSC),.CORE_CLOCK_HZ(32000000),
        .ENABLE_EXTENSIONS(1),.SID_COUNT(8)) dut(
        .core_clk(clk),.cpu_clk(fastclk),.reset(reset),.core_reset_request(1'b0),
        .cpu_config(config_word),.cpu_status(cpu_status),.cpu_turbo_enable(1'b0),
        .ua_enable(ua_enable),.debug_enable(1'b0),.uci_enable(uci_enable),.uci_control(control),
        .uci_op(op),.uci_op_valid(op_valid),.uci_op_ready(),.uci_ram_request(32'd0),
        .uci_status(uci_status),.uci_status2(uci_status2),
        .uci_buffer_addr(buffer_addr),.uci_buffer_write(buffer_we),
        .uci_buffer_wdata(buffer_wdata),.uci_buffer_rdata(buffer_rdata),
        .sid_map_enable(map_enable),.sid_slot_masks(256'd0),.sid_params(256'd0),
        .sid_write_inhibit(32'd0),.sid_output_config(32'd0),.sid_owners(96'd0),.sid_io_masks(io_masks),
        .peek_word(32'd0),.reu_host_word(32'd0),
        .ua_memory_ready(1'b1),.ua_memory_rvalid(1'b0),.ua_memory_fault(1'b0),.ua_memory_rdata(64'd0),
        .iec9_run(1'b0),.iec9_quiesce(1'b0),.iec9_event_ready(1'b1),
        .iec9_reply_data(32'd0),.iec9_reply_valid(1'b0),
        .pause_request(pause_request),.drive_parallel_enable(1'b0),.sid_mode(3'd0),.reu_enable(reu_enable),
        .load_abort(1'b0),.load_abort_epoch(1'b0),.clear_load_error(1'b0),
        .input_code(16'd0),.input_value(16'd0),.input_valid(1'b0),
        .load_data(load_data),.load_valid(load_valid),.load_ready(load_ready),
        .load_first(load_first),.load_last(load_last),.load_kind(load_kind),
        .load_offset(load_offset),.load_total(load_total),.load_epoch(1'b0),
        .memory_addr(cart_addr),.memory_write(cart_write),.memory_valid(cart_valid),
        .memory_wdata(cart_wdata),.memory_wstrb(cart_wstrb),
        .memory_ready(cart_ready),.memory_rdata(64'd0),.memory_rvalid(cart_rvalid),.memory_fault(1'b0),
        .reu_memory_addr(ddr_addr),.reu_memory_write(ddr_write),.reu_memory_wdata(ddr_wdata),
        .reu_memory_wstrb(ddr_wstrb),.reu_memory_valid(ddr_valid),.reu_memory_burst(ddr_burst),
        .reu_memory_busy(ddr_busy),.reu_memory_ready(ddr_ready),.reu_memory_rdata(ddr_rdata),
        .reu_memory_rvalid(ddr_rvalid),.reu_memory_fault(1'b0),
        .media_mount(1'b0),.media_eject(1'b0),.media_complete(1'b0),.media_slot(8'd0),
        .media_kind(8'd0),.media_read_only(1'b1),.media_image_size(32'd0),
        .media_generation(32'd0),.media_complete_request_id(32'd0),.media_complete_status(32'd0),
        .media_complete_block_count(16'd0),.media_complete_generation(32'd0),.media_write_data_ready(1'b1),
        .ready(ready),.load_error(load_error));
    integer checks=0;reg [15:0] probe_addr=0;reg probe_we=0;reg [7:0] probe_data=0;
    task automatic check(input bit ok,input string why);
        checks++;if(!ok)$fatal(1,"ADAPTER LOAD SID %s",why);
    endtask
    always @(posedge clk)begin
        cart_rvalid<=0;
        if(cart_valid&&cart_ready)begin
            check(cart_write,"CRT backend write");
            for(integer b=0;b<8;b++)if(cart_wstrb[b])begin
                check(cart_addr+b>=24'h200000 && cart_addr+b<24'h202000,"CRT address bounded");
                cart_backing[cart_addr+b-24'h200000]<=cart_wdata[b*8+:8];cart_bytes++;
            end
            cart_pending<=1;cart_delay<=4+(cart_bytes%7);
        end
        if(cart_pending)begin
            if(cart_delay==0)begin cart_pending<=0;cart_rvalid<=1;end
            else cart_delay<=cart_delay-1;
        end
        if(dut.uci_ram_write)uci_bytes++;
    end
    function automatic [7:0] crt_byte(input integer a);
        reg [127:0] magic;
        begin
            magic="C64 CARTRIDGE   ";crt_byte=0;
            if(a<16)crt_byte=magic[(15-a)*8+:8];
            case(a)
            19:crt_byte=8'h40;20:crt_byte=1;25:crt_byte=1;
            64:crt_byte="C";65:crt_byte="H";66:crt_byte="I";67:crt_byte="P";
            70:crt_byte=8'h20;71:crt_byte=8'h10;76:crt_byte=8'h80;78:crt_byte=8'h20;
            default:if(a>=80)crt_byte=a[7:0]^8'ha5;
            endcase
        end
    endfunction
    task automatic send(input [7:0] kind,input integer a,total,input [7:0] value);
        integer waited;
        @(negedge clk);load_kind=kind;load_offset=a;load_total=total;load_data=value;
        load_first=a==0;load_last=a==total-1;load_valid=1;waited=0;
        // Sample ready before the accepting edge, not after state advances.
        while(!load_ready&&waited<256)begin @(negedge clk);waited++;end
        check(load_ready,"loader handshake bounded");
        @(posedge clk);@(negedge clk);load_valid=0;load_first=0;load_last=0;
    endtask
    task automatic wait_native_read;
        integer n;
        n=0;while(!dut.ext_cpu_enable&&n<128)begin @(negedge clk);n++;end
        check(n<128,"native bus slot available");#1;
    endtask
    initial begin
        $readmemh("kernal.hex",rom);repeat(8)@(negedge clk);reset=0;
        // Actual generic CRT stream through adapter and delayed cart backend.
        // No HOLD: the old decimal17 UCI ready mux deadlocks immediately.
        for(integer a=0;a<8272;a++)send(8'h11,a,8272,crt_byte(a));
        repeat(128)@(negedge clk);
        check(dut.cart_loaded&&!dut.cart_loading&&!load_error,"CRT finalized");
        check(cart_bytes==8192&&uci_bytes==0,"CRT exclusively reached cartridge storage");
        for(integer a=0;a<8192;a++)check(cart_backing[a]===(((a+80)^8'ha5)&255),"exact CRT bytes");
        send(8'h14,0,1,0);repeat(32)@(negedge clk);check(!dut.cart_loaded,"detach");
        // Bootstrap synthetic ROMs, then physical RAM upload via distinct17.
        for(integer kind=1;kind<=3;kind++)for(integer a=0;a<(kind==3?4096:8192);a++)
            send(kind,a,kind==3?4096:8192,kind==2?rom[a]:8'hea);
        control[10]=1;repeat(256)@(negedge clk);check(dut.core_paused,"UCI HOLD acknowledged");
        for(integer a=0;a<6;a++)send(8'h17,a,6,a==0?8'h00:a==1?8'h30:8'h90+a);
        repeat(8)@(negedge clk);check(uci_bytes==4&&!load_error,"UCI RAM packet accepted once");
        for(integer a=0;a<4;a++)check(dut.main_ram_i.main_low[16'h3000+a]==8'h92+a,"UCI exact RAM bytes");
        // Bus injection at the external CPU pins, through real PLA and eight
        // SID engines. RAM initialization supplies a defined VIC open bus.
        for(integer a=0;a<32768;a++)begin dut.main_ram_i.main_low[a]=8'h5a;dut.main_ram_i.main_high[a]=8'h5a;end
        for(integer a=0;a<1024;a++)dut.main_ram_i.colour[a]=4'h5;
        control[10]=0;pause_request=0;repeat(20000)@(negedge clk);
        force dut.cpu_island_i.cpu_enable=0;
        force dut.ext_cpu_addr=probe_addr;force dut.ext_cpu_we=probe_we;
        force dut.ext_cpu_do=probe_data;force dut.ext_cpu_io=8'h37;
        control[10]=0;pause_request=0;probe_addr=16'hd011;probe_we=1;probe_data=0;
        repeat(128)@(negedge clk);probe_we=0;map_enable=1;
        for(integer slot=0;slot<16;slot++)begin
            io_masks=0;probe_addr=16'hde00+slot*32;
            wait_native_read();
            begin integer retries;retries=0;
                while($isunknown(dut.ext_last_vic_di)&&retries<128)begin
                    @(negedge clk);while(dut.ext_cpu_enable)@(negedge clk);
                    wait_native_read();retries++;
                end
            end
            check(!dut.sid_io_select,"unmapped SID slot does not own IO");
            $display("FLOAT slot=%0d phase=%0d addr=%h busaddr=%h di=%h last=%h ioe=%b iof=%b owned=%b",slot,dut.ext_phase,probe_addr,dut.cpu_bus_addr,dut.ext_cpu_di,dut.ext_last_vic_di,dut.cart_ioe,dut.cart_iof,dut.ext_rdy);
            check(!$isunknown(dut.ext_last_vic_di)&&dut.ext_cpu_di===dut.ext_last_vic_di,"unmapped IO returns real VIC floating bus");
            @(negedge clk);io_masks[slot*8+:8]=8'h80;probe_we=1;probe_data=8'h80+slot;
            repeat(64)@(negedge clk);probe_we=0;wait_native_read();
            check(dut.sid_io_select&&dut.ext_cpu_di==8'h80+slot,"mapped SID7 native readback");
            @(negedge clk);
        end
        // Higher-priority device apertures win even with all SID slots set.
        io_masks={128{1'b1}};reu_enable=1;ua_enable=1;uci_enable=1;
        probe_addr=16'hdf00;wait_native_read();check(dut.reu_io_select&&!dut.sid_io_select,"REU priority");@(negedge clk);
        probe_addr=16'hdf1c;wait_native_read();check(dut.uci_io_select&&!dut.sid_io_select,"UCI priority");@(negedge clk);
        probe_addr=16'hdf20;wait_native_read();check(dut.ua_io_select&&!dut.sid_io_select,"UA priority");
        $display("PASS ADAPTER LOAD SID checks=%0d CRTbytes=%0d UCIbytes=%0d slots=16 NTSC=%0d",checks,cart_bytes,uci_bytes,NTSC);$finish;
    end
    initial begin #100000000;$fatal(1,"ADAPTER LOAD SID timeout");end
endmodule


// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Full adapter: actual T65/island/proxy, UCI, VIC/CIA, REU and REU cache.
// Only ROM contents, host UCI buffer/operations and DDR responses are fixtures.
module tb_c64_cart_coexist #(parameter integer NTSC=0);
    reg clk=0,fastclk=0;
    initial forever begin
        #7.8125;clk=1;fastclk=1;
        #7.8125;fastclk=0;
        #7.8125;clk=0;fastclk=1;
        #7.8125;fastclk=0;
    end
    reg reset=1,pause_request=1,guest_reset=0,uci_config_epoch=0;
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
    reg map_enable=0,reu_enable=1,ua_enable=1,uci_enable=1;
    reg load_abort=0,clear_load_error=0;
    reg [127:0] io_masks=0;
    wire [23:0] cart_addr;wire cart_write,cart_valid;
    wire [63:0] cart_wdata;wire [7:0] cart_wstrb;
    reg cart_rvalid=0;reg [63:0] cart_rdata=0;reg [7:0] cart_backing[0:2097151];
    reg [23:0] cart_pending_addr; reg cart_pending_write;
    reg [63:0] cart_pending_data; reg [7:0] cart_pending_strb;
    reg cart_pending=0;integer cart_delay=0,cart_bytes=0,uci_bytes=0;
    reg bulk_load=0;
    reg [31:0] cart_erase_request=0;wire [31:0] cart_erase_status;
    reg erase_test=0;integer erase_words=0;
    reg drive_power_off=0,ef_boot_disable=0;
    wire [31:0] drive_status;
    wire cart_ready=!cart_pending;
    multitini_c64_adapter #(.NTSC_MODE(NTSC),.CORE_CLOCK_HZ(32000000),
        .ENABLE_EXTENSIONS(1),.SID_COUNT(8)) dut(
        .core_clk(clk),.cpu_clk(fastclk),.reset(reset),.core_reset_request(guest_reset),
        .cpu_config(config_word),.cpu_status(cpu_status),.cpu_turbo_enable(1'b0),
        .ua_enable(ua_enable),.debug_enable(1'b0),.uci_enable(uci_enable),
        .uci_config_epoch(uci_config_epoch),.uci_control(control),
        .cart_erase_request(cart_erase_request),.cart_erase_status(cart_erase_status),
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
        .pause_request(pause_request),.drive_parallel_enable(1'b0),.drive_power_off(drive_power_off),.ef_boot_disable(ef_boot_disable),.drive_status(drive_status),.sid_mode(3'd0),.reu_enable(reu_enable),
        .load_abort(load_abort),.load_abort_epoch(1'b0),.clear_load_error(clear_load_error),
        .input_code(16'd0),.input_value(16'd0),.input_valid(1'b0),
        .load_data(load_data),.load_valid(load_valid),.load_ready(load_ready),
        .load_first(load_first),.load_last(load_last),.load_kind(load_kind),
        .load_offset(load_offset),.load_total(load_total),.load_epoch(1'b0),
        .memory_addr(cart_addr),.memory_write(cart_write),.memory_valid(cart_valid),
        .memory_wdata(cart_wdata),.memory_wstrb(cart_wstrb),
        .memory_ready(cart_ready),.memory_rdata(cart_rdata),.memory_rvalid(cart_rvalid),.memory_fault(1'b0),
        .reu_memory_addr(ddr_addr),.reu_memory_write(ddr_write),.reu_memory_wdata(ddr_wdata),
        .reu_memory_wstrb(ddr_wstrb),.reu_memory_valid(ddr_valid),.reu_memory_burst(ddr_burst),
        .reu_memory_busy(ddr_busy),.reu_memory_ready(ddr_ready),.reu_memory_rdata(ddr_rdata),
        .reu_memory_rvalid(ddr_rvalid),.reu_memory_fault(1'b0),
        .media_mount(1'b0),.media_eject(1'b0),.media_complete(1'b0),.media_slot(8'd0),
        .media_kind(8'd0),.media_read_only(1'b1),.media_image_size(32'd0),
        .media_generation(32'd0),.media_complete_request_id(32'd0),.media_complete_status(32'd0),
        .media_complete_block_count(16'd0),.media_complete_generation(32'd0),.media_write_data_ready(1'b1),
        .ready(ready),.load_error(load_error));
    defparam dut.turbo_regs_i.RESET_WINDOW_CYCLES=256;
    integer checks=0,cart_reads=0,dma_bytes=0,profiles=0;
    reg [30:0] ef_journal[0:63];integer ef_entries=0;
    reg [15:0] probe_addr=16'h0400;
    reg probe_we=0;
    reg [7:0] probe_data=0;
    task automatic check(input bit ok,input string why);
        checks++;if(!ok)$fatal(1,"CART COEXIST %s",why);
    endtask
    always @(posedge clk)begin
        if(buffer_we)buffer[buffer_addr]<=buffer_wdata;
        buffer_rdata<=buffer[buffer_addr];
        cart_rvalid<=0;ddr_rvalid<=0;
        if(cart_valid&&cart_ready)begin
            check(cart_addr>=24'h200000 && cart_addr<24'h400000,
                $sformatf("cartridge backing range addr=%h id=%0d loading=%b bank=%h/%h count=%h vic=%b ram=%h phase=%0d",
                cart_addr,dut.loader_cart_id,dut.cart_loading,dut.cartridge_i.bank_lo,
                dut.cartridge_i.bank_hi,dut.cartridge_i.bank_cnt,dut.cart_umax_rom_h,
                dut.ram_addr,dut.ext_phase));
            // Maximum-capacity streams use one response clock per request;
            // normal loads and all runtime reads keep variable memory delay.
            cart_pending<=1;cart_delay<=bulk_load?0:4+(cart_bytes%5);
            cart_pending_addr<=cart_addr-24'h200000;cart_pending_write<=cart_write;
            cart_pending_data<=cart_wdata;cart_pending_strb<=cart_wstrb;
        end
        if(cart_pending)begin
            if(cart_delay==0)begin
                for(integer b=0;b<8;b++)begin
                    if(cart_pending_write&&cart_pending_strb[b])begin
                        cart_backing[cart_pending_addr+b]=cart_pending_data[b*8+:8];cart_bytes++;
                    end
                    cart_rdata[b*8+:8]<=cart_backing[cart_pending_addr+b];
                end
                if(!cart_pending_write)cart_reads++;
                cart_pending<=0;cart_rvalid<=1;
            end else cart_delay<=cart_delay-1;
        end
        if(ddr_valid&&ddr_ready)begin
            check(ddr_addr<480,"REU backing range");
            pending<=1;pending_addr<=ddr_addr;pending_write<=ddr_write;
            pending_data<=ddr_wdata;pending_strb<=ddr_wstrb;
            delay_count<=12+(requests%3)*8;requests<=requests+1;
            beats_left<=ddr_burst?4:1;beat_index<=0;
        end
        if(pending)begin
            if(delay_count!=0)delay_count<=delay_count-1;
            else begin
                for(integer b=0;b<8;b++)begin
                    if(pending_write&&pending_strb[b])backing[pending_addr+b]=pending_data[b*8+:8];
                    ddr_rdata[b*8+:8]<=backing[pending_addr+beat_index*8+b];
                end
                ddr_rvalid<=1;
                if(beats_left==1)pending<=0;
                else begin beats_left<=beats_left-1;beat_index<=beat_index+1;delay_count<=7;end
            end
        end
        if(dut.reu_dma_ack)dma_bytes++;
        if(dut.ef_journal_valid)begin ef_journal[ef_entries]=dut.ef_journal_data;ef_entries++;end
    end
    function automatic [7:0] crt_byte(input integer a,type_id,subtype,banks,bank_size);
        reg [127:0] magic;integer packet,offset;
        begin
            magic="C64 CARTRIDGE   ";crt_byte=0;
            if(a<16)crt_byte=magic[(15-a)*8+:8];
            if(a==19)crt_byte=8'h40;
            if(a==20)crt_byte=1;
            if(a==23)crt_byte=type_id;
            if(a==24)crt_byte=type_id==71;
            if(a==25)crt_byte=bank_size==8192&&type_id!=54;
            if(a==26)crt_byte=subtype;
            if(a>=64)begin
                packet=(a-64)/(bank_size+16);offset=(a-64)%(bank_size+16);
                case(offset)
                0:crt_byte="C";1:crt_byte="H";2:crt_byte="I";3:crt_byte="P";
                6:crt_byte=(bank_size+16)>>8;7:crt_byte=8'h10;
                11:crt_byte=packet;12:crt_byte=8'h80;14:crt_byte=bank_size>>8;
                default:if(offset>=16)crt_byte=8'h40+packet+
                    ((type_id!=3&&bank_size==16384&&offset>=8208)?8'h20:0);
                endcase
            end
        end
    endfunction
    // CRT type 0 in each admitted layout: 8 KiB, one 16 KiB packet, two
    // 8 KiB packets and Ultimax. Payload bytes identify layout and half.
    // Dense canonical EasyFlash stream as firmware sends it: packet i is
    // bank i/2, ROML then ROMH. Payload byte 0x20+i identifies the slot.
    function automatic [7:0] easyflash_byte(input integer a);
        reg [127:0] magic;integer packet,offset;
        begin
            magic="C64 CARTRIDGE   ";easyflash_byte=0;
            if(a<16)easyflash_byte=magic[(15-a)*8+:8];
            if(a==19)easyflash_byte=8'h40;
            if(a==20)easyflash_byte=1;
            if(a==23)easyflash_byte=32;
            if(a==24)easyflash_byte=1;
            if(a>=64)begin
                packet=(a-64)/8208;offset=(a-64)%8208;
                case(offset)
                0:easyflash_byte="C";1:easyflash_byte="H";2:easyflash_byte="I";3:easyflash_byte="P";
                6:easyflash_byte=8'h20;7:easyflash_byte=8'h10;
                11:easyflash_byte=packet/2;12:easyflash_byte=packet[0]?8'ha0:8'h80;
                14:easyflash_byte=8'h20;
                default:if(offset>=16)easyflash_byte=8'h20+packet;
                endcase
            end
        end
    endfunction
    function automatic [7:0] normal_byte(input integer a,layout);
        reg [127:0] magic;integer chip_size,packet,offset;
        begin
            magic="C64 CARTRIDGE   ";normal_byte=0;chip_size=layout==1?16384:8192;
            if(a<16)normal_byte=magic[(15-a)*8+:8];
            if(a==19)normal_byte=8'h40;
            if(a==20)normal_byte=1;
            if(a==24)normal_byte=layout==3;
            if(a==25)normal_byte=layout==0;
            if(a>=64)begin
                packet=(a-64)/(chip_size+16);offset=(a-64)%(chip_size+16);
                case(offset)
                0:normal_byte="C";1:normal_byte="H";2:normal_byte="I";3:normal_byte="P";
                6:normal_byte=(chip_size+16)>>8;7:normal_byte=8'h10;
                12:normal_byte=layout==3?8'he0:packet?8'ha0:8'h80;
                14:normal_byte=chip_size>>8;
                default:if(offset>=16)normal_byte=8'h90+layout*2+packet+(offset>=8208);
                endcase
            end
        end
    endfunction
    // A bulk stream keeps load_valid high between bytes, as a back-to-back
    // source does, so the loader takes a byte on every clock it is ready.
    // Other loads drop valid after each byte.
    task automatic send(input [7:0] kind,input integer a,total,input [7:0] value);
        integer waited;
        @(negedge clk);load_kind=kind;load_offset=a;load_total=total;load_data=value;
        load_first=a==0;load_last=a==total-1;load_valid=1;waited=0;
        while(!load_ready&&waited<256)begin @(negedge clk);waited++;end
        check(load_ready,"loader handshake bounded");
        @(posedge clk);
        if(!bulk_load||load_last)begin @(negedge clk);load_valid=0;load_first=0;load_last=0;end
    endtask
    task automatic twomegabyter;
        integer initial_bytes,waited;
        attach(87,0,4,16384,7);active_state(7,"TMB permits REU/UCI/audio");
        read_value(16'h8000,8'h40,"TMB reset low bank zero");
        read_value(16'ha000,8'h60,"TMB reset high bank zero");
        // Every data value, including bit7, mirrors the admitted 64 KiB ROM.
        // All IO1 addresses decode only A1; alternate bank-register mirrors.
        for(integer bank=0;bank<256;bank++)begin
            native_write(bank[0]?16'hdefd:16'hde00,bank[7:0]);
            read_value(16'h8000,8'h40+(bank&3),"TMB low bank and size mirroring");
            read_value(16'ha000,8'h60+(bank&3),"TMB high bank and size mirroring");
            check(!dut.cart_game&&!dut.cart_exrom,"TMB bank bit7 never disables ROM");
        end
        for(integer mode=0;mode<4;mode++)begin
            native_write(16'hdeff,8'h80|mode);
            check({dut.cart_exrom,dut.cart_game}==mode[1:0],"TMB independent mode bits and ignored LED");
            if(mode==0)read_value(16'ha000,8'h63,"TMB sixteen-K mode");
            if(mode==1)begin
                read_value(16'h8000,8'h43,"TMB eight-K mode low ROM");
                read_value(16'ha000,8'hea,"TMB eight-K mode exposes BASIC");
            end
            if(mode==2)read_value(16'he000,8'h63,"TMB Ultimax high ROM");
            if(mode==3)read_value(16'h8000,8'h80,"TMB disabled mode exposes RAM");
        end
        native_write(16'hde02,0);native_write(16'h8001,8'ha6);
        read_value(16'h8001,8'h43,"TMB ROM write leaves ROM unchanged");
        native_write(16'hde03,3);read_value(16'h8001,8'ha6,"TMB write underneath ROM reaches RAM");
        native_write(16'hde01,2);native_write(16'hde02,0);
        read_value(16'h8000,8'h42,"TMB bank can change while disabled");
        @(negedge clk);probe_addr=16'hde00;probe_we=0;
        repeat(128)@(negedge clk);
        check(!dut.cart_io_rom&&!dut.cartridge_i.IO_rd,"TMB IO1 read has no readback or ROM mirror");
        probe_addr=16'h0400;
        read_value(16'h8000,8'h42,"TMB IO1 read does not switch bank");
        native_write(16'hdf02,8'h5c);read_value(16'hdf02,8'h5c,"TMB REU register readback");
        read_value(16'hdf1d,8'hc9,"TMB UCI identification");
        read_value(16'h8000,8'h42,"TMB IO2 access leaves bank unchanged");
        // Real REU DMA reads selected cartridge ROM through the adapter/cache.
        native_write(16'hdf02,0);native_write(16'hdf03,8'h80);
        native_write(16'hdf04,8'h80);native_write(16'hdf05,1);native_write(16'hdf06,0);
        native_write(16'hdf07,16);native_write(16'hdf08,0);
        native_write(16'hdf09,0);native_write(16'hdf0a,0);
        initial_bytes=dma_bytes;native_write(16'hdf01,8'h90);waited=0;
        while((dma_bytes-initial_bytes<16||dut.reu_memory_busy)&&waited<100000)begin
            @(negedge clk);waited++;
        end
        check(waited<100000&&dma_bytes-initial_bytes==16,"TMB REU DMA completes");
        for(integer a=0;a<16;a++)check(backing[384+a]===8'h42,"TMB REU DMA selected ROM bytes");
        guest_reset=1;repeat(32)@(negedge clk);guest_reset=0;repeat(512)@(negedge clk);
        video_idle();active_state(7,"TMB reset preserves expansion policy");
        read_value(16'h8000,8'h40,"TMB warm reset restores bank zero");
        check(!dut.cart_game&&!dut.cart_exrom,"TMB warm reset restores sixteen-K mode");
        $display("TMB PASS NTSC=%0d banks=256 modes=4 DMA=16",NTSC);
    endtask
    task automatic attach(input integer id,subtype,banks,bank_size,policy);
        integer total,before_bytes;
        probe_addr=16'h0400;probe_we=0;total=64+banks*(bank_size+16);before_bytes=cart_bytes;
        for(integer a=0;a<total;a++)send(8'h11,a,total,crt_byte(a,id,subtype,banks,bank_size));
        repeat(512)@(negedge clk);
        check(dut.cart_loaded&&!dut.cart_loading&&!load_error,"cartridge finalized");
        check(dut.loader_cart_id==id&&dut.loader_cart_subtype==subtype,"committed type/subtype");
        check(dut.loader_cart_expansion_policy==policy,"committed expansion policy");
        check(cart_bytes-before_bytes==banks*bank_size,"all cartridge writes acknowledged");
        profiles++;
    endtask
    task automatic attach_normal(input integer layout);
        integer total,chip_bytes,before_bytes;
        probe_addr=16'h0400;probe_we=0;before_bytes=cart_bytes;
        chip_bytes=layout==1||layout==2?16384:8192;
        total=64+chip_bytes+(layout==2?32:16);
        for(integer a=0;a<total;a++)send(8'h11,a,total,normal_byte(a,layout));
        repeat(512)@(negedge clk);
        check(dut.cart_loaded&&!dut.cart_loading&&!load_error,"normal cartridge finalized");
        check(dut.loader_cart_id==0&&dut.loader_cart_expansion_policy==7,"normal type and policy");
        check(cart_bytes-before_bytes==chip_bytes,"normal cartridge writes acknowledged");
    endtask
    task automatic native_write(input [15:0] address,input [7:0] value);
        @(negedge clk);probe_addr=address;probe_data=value;probe_we=1;
        repeat(64)begin
            @(negedge clk);
            if(address==16'hdfff&&dut.cartridge_i.stb_iof)
                $display("BANK_WRITE phase=%0d addr=%h data=%h we=%b iof=%b bank=%h disabled=%b reset=%b id=%0d",dut.ext_phase,dut.ram_addr,dut.ram_dout,dut.ram_we,dut.cartridge_i.iof_wr,dut.cartridge_i.bank_lo,dut.cartridge_i.cart_disable,dut.core_reset_n,dut.loader_cart_id);
        end
        probe_we=0;probe_addr=16'h0400;
        repeat(64)@(negedge clk);
    endtask
    task automatic read_value(input [15:0] address,input [7:0] expected,input string why);
        integer waited,retired;
        @(negedge clk);probe_addr=address;probe_we=0;waited=0;
        retired=0;
        // A cartridge read can stall the native CPU until the delayed word
        // reaches the real cartridge cache. Observe the accepted CPU slot.
        repeat(128)@(negedge clk);
        while((!dut.ext_cpu_enable || dut.ext_cpu_di!==expected)&&waited<2048)begin
            if(dut.ext_cpu_enable)begin
                if(retired<4)$display("RETIRE address=%h expected=%h actual=%h phase=%0d external=%b request=%b",address,expected,dut.ext_cpu_di,dut.ext_phase,dut.cart_external_select,dut.cart_runtime_request);
                retired++;
            end
            @(negedge clk);waited++;
        end
        if(waited>=2048)$display("READ address=%h expected=%h actual=%h bus=%h CE=%b stall=%b cartreads=%0d reset=%b extreset=%b paused=%b phase=%0d frame=%b dma=%b",address,expected,dut.ext_cpu_di,dut.cpu_bus_addr,dut.ext_cpu_enable,dut.cart_stall,cart_reads,dut.core_reset_n,dut.ext_reset,dut.core_paused,dut.ext_phase,dut.ext_frame_run,dut.reu_dma_req);
        check(waited<2048,why);
        @(negedge clk);probe_addr=16'h0400;
    endtask
    task automatic active_state(input [2:0] expected,input string why);
        repeat(16)@(negedge clk);
        check({dut.ua_active,dut.uci_active,dut.reu_active}==expected,why);
        check(dut.reu_host_status[16]==expected[0]&&dut.reu_host_status[17]==expected[2]&&
              dut.reu_host_status[18]==expected[1],"effective status flags");
    endtask
    task automatic native_touch(input [15:0] address);
        @(negedge clk);probe_addr=address;probe_we=0;
        repeat(128)@(negedge clk);
        probe_addr=16'h0400;repeat(64)@(negedge clk);
    endtask
    task automatic warm_reset;
        guest_reset=1;repeat(32)@(negedge clk);guest_reset=0;
        repeat(512)@(negedge clk);video_idle();
    endtask
    task automatic video_idle;
        // The synthetic KERNAL does not initialize VIC registers. Give its
        // inactive display and sprites defined addresses even in Ultimax.
        native_write(16'hd011,0);native_write(16'hd015,0);
        native_write(16'hd016,0);native_write(16'hd018,0);
    endtask
    task automatic rom_simple;
        attach(11,0,1,16384,0);active_state(0,"Westermann blocks IO2 expansions");
        read_value(16'h8000,8'h40,"Westermann low ROM");
        read_value(16'ha000,8'h60,"Westermann high ROM");
        native_write(16'hdf7e,0);read_value(16'ha000,8'h60,"Westermann ignores IO2 writes");
        native_touch(16'hdf7e);read_value(16'ha000,8'hea,"Westermann IO2 read disables upper ROM");
        read_value(16'h8000,8'h40,"Westermann keeps lower ROM after IO2 read");
        native_touch(16'hde00);read_value(16'ha000,8'hea,"Westermann IO1 read cannot reenable");
        warm_reset();read_value(16'ha000,8'h60,"Westermann reset restores 16K");
        $display("ROM_MAPPER PASS NTSC=%0d id=11",NTSC);

        attach(54,0,3,8192,7);active_state(7,"Kingsoft allows IO2 expansions");
        read_value(16'h8000,8'h40,"Kingsoft reset low ROM");
        read_value(16'ha000,8'h41,"Kingsoft reset high ROM");
        native_write(16'hdeff,8'hff);
        read_value(16'h8000,8'h40,"Kingsoft alternate low ROM");
        read_value(16'he000,8'h42,"Kingsoft alternate E000 ROM");
        read_value(16'hc000,8'h5a,"Kingsoft alternate mode retains ordinary RAM");
        read_value(16'hdf1d,8'hc9,"Kingsoft alternate mode retains UCI");
        native_touch(16'hde55);
        read_value(16'ha000,8'h41,"Kingsoft IO1 read restores normal high ROM");
        native_write(16'hde00,0);warm_reset();
        read_value(16'ha000,8'h41,"Kingsoft warm reset restores 16K");
        $display("ROM_MAPPER PASS NTSC=%0d id=54",NTSC);

        attach(65,0,1,8192,0);active_state(0,"BB3 blocks expansions");
        read_value(16'h8000,8'h40,"BB3 reset ROM");
        native_touch(16'hde12);read_value(16'h8000,8'h40,"BB3 IO1 read has no effect");
        native_write(16'hde12,0);read_value(16'h8000,8'h80,"BB3 IO1 write disables");
        native_touch(16'hdf12);read_value(16'h8000,8'h80,"BB3 IO2 read has no effect");
        native_write(16'hdf12,0);read_value(16'h8000,8'h40,"BB3 IO2 write reenables");
        native_write(16'hdeff,0);warm_reset();read_value(16'h8000,8'h40,"BB3 reset reenables");
        $display("ROM_MAPPER PASS NTSC=%0d id=65",NTSC);

        attach(66,0,1,16384,0);active_state(0,"BB4 blocks expansions");
        read_value(16'ha000,8'h60,"BB4 reset high ROM");
        native_write(16'hdf33,0);read_value(16'h8000,8'h40,"BB4 IO2 write has no effect");
        native_touch(16'hdf33);read_value(16'h8000,8'h80,"BB4 IO2 read disables");
        native_write(16'hde33,0);read_value(16'h8000,8'h80,"BB4 IO1 write has no effect");
        native_touch(16'hde33);read_value(16'ha000,8'h60,"BB4 IO1 read reenables");
        native_touch(16'hdfff);warm_reset();read_value(16'ha000,8'h60,"BB4 reset reenables");
        $display("ROM_MAPPER PASS NTSC=%0d id=66",NTSC);
    endtask
    task automatic blackboxes;
        integer address, selected;
        // BB8 control is the IO2 address, independent of the written value.
        for(integer banks=2;banks<=4;banks=banks*2)begin
            attach(64,0,banks,16384,0);active_state(0,"BB8 blocks IO2 expansions");
            read_value(16'h8000,8'h40+banks-1,"BB8 starts in last populated bank");
            for(integer control_bits=0;control_bits<16;control_bits++)begin
                address=16'hdff0|control_bits;selected=(~(control_bits>>2))&(banks-1);
                native_write(address,8'ha5^control_bits);
                check({dut.cart_exrom,dut.cart_game}==control_bits[1:0],"BB8 address controls mode");
                if(control_bits[1:0]==0)begin
                    read_value(16'h8000,8'h40+selected,"BB8 selected low bank");
                    read_value(16'ha000,8'h60+selected,"BB8 selected high bank");
                end
                if(control_bits[1:0]==1)read_value(16'h8000,8'h40+selected,"BB8 8K mode");
                if(control_bits[1:0]==2)read_value(16'he000,8'h60+selected,"BB8 Ultimax mode");
                if(control_bits[1:0]==3)read_value(16'h8000,8'h80,"BB8 disabled mode");
            end
            native_write(16'hdf0c,0);native_touch(16'hdf00);
            read_value(16'h8000,8'h40,"BB8 IO2 reads do not switch");
            warm_reset();read_value(16'h8000,8'h40+banks-1,"BB8 reset restores last bank");
            $display("BB8_SIZE PASS NTSC=%0d banks=%0d",NTSC,banks);
        end
        $display("ROM_MAPPER PASS NTSC=%0d id=64",NTSC);

        attach(71,0,2,16384,7);active_state(7,"BB9 permits IO2 expansions");
        read_value(16'he000,8'h61,"BB9 reset Ultimax last bank");
        for(integer writing=0;writing<2;writing++)for(integer bits=0;bits<8;bits++)begin
            address=16'hde00|((bits&4)<<5)|((bits&2)<<5)|(bits&1);
            selected=writing?1-((bits>>2)&1):((bits>>2)&1);
            if(writing)native_write(address,8'hff-bits);
            else read_value(address,8'h40+selected,"BB9 IO1 returns new selected bank mirror");
            check({dut.cart_exrom,dut.cart_game}==bits[1:0],"BB9 address controls independent mode lines");
            if(bits[1:0]==0)begin
                read_value(16'h8000,8'h40+selected,"BB9 16K selected low bank");
                read_value(16'ha000,8'h60+selected,"BB9 16K selected high bank");
            end
            if(bits[1:0]==1)read_value(16'h8000,8'h40+selected,"BB9 8K selected bank");
            if(bits[1:0]==2)read_value(16'he000,8'h60+selected,"BB9 Ultimax selected bank");
            if(bits[1:0]==3)read_value(16'h8000,8'h80,"BB9 disabled exposes RAM");
        end
        read_value(16'hdf1d,8'hc9,"BB9 UCI unaffected by IO1");
        warm_reset();read_value(16'he000,8'h61,"BB9 reset returns last bank Ultimax");
        $display("ROM_MAPPER PASS NTSC=%0d id=71",NTSC);
    endtask
    task automatic megabyter;
        attach(86,0,4,8192,7);active_state(7,"Megabyter permits IO2 expansions");
        for(integer bank=0;bank<256;bank++)begin
            native_write(bank[0]?16'hdefd:16'hde00,bank);
            read_value(16'h8000,8'h40+(bank&3),"Megabyter bank mirroring includes bit7");
        end
        for(integer mode=0;mode<4;mode++)begin
            native_write(16'hdeff,8'h80|mode);
            check({dut.cart_exrom,dut.cart_game}=={mode[1],!mode[0]},"Megabyter mode polarity and ignored LED");
            if(mode<2)read_value(16'h8000,8'h43,"Megabyter enabled low ROM");
            if(mode==2)read_value(16'h8000,8'h80,"Megabyter disabled mode");
        end
        native_write(16'hde02,0);native_touch(16'hde00);
        read_value(16'h8000,8'h43,"Megabyter IO1 reads do not select bank");
        read_value(16'hdf1d,8'hc9,"Megabyter UCI remains visible");
        warm_reset();read_value(16'h8000,8'h40,"Megabyter reset first bank");
        check(!dut.cart_exrom&&dut.cart_game,"Megabyter reset 8K mode");
        $display("ROM_MAPPER PASS NTSC=%0d id=86",NTSC);
    endtask
    task automatic upper_banks;
        integer id,banks,size,selected;
        for(integer profile=0;profile<2;profile++)begin
            id=profile==0?86:87;banks=profile==0?256:128;size=profile==0?8192:16384;
            bulk_load=1;attach(id,0,banks,size,7);bulk_load=0;
            for(integer bank=0;bank<256;bank++)begin
                native_write(16'hde00,bank);selected=bank&(banks-1);
                read_value(16'h8000,8'h40+selected,"full capacity physical low bank");
                if(id==87)read_value(16'ha000,8'h60+selected,"full capacity physical high bank");
            end
            $display("ROM_CAPACITY PASS NTSC=%0d id=%0d bytes=2097152 selections=256",NTSC,id);
        end
        $display("ROM_MAPPER PASS NTSC=%0d id=87",NTSC);
    endtask
    // native_write holds a write across two native cycles, which suits
    // idempotent registers. Flash command sequences count every write, so
    // their writes occupy exactly one native cycle, as one STA does.
    task automatic flash_write(input [15:0] address,input [7:0] value);
        integer waited;
        @(negedge clk);waited=0;
        while(!dut.ext_cpu_enable&&waited<256)begin @(negedge clk);waited++;end
        probe_addr=address;probe_data=value;probe_we=1;
        @(negedge clk);waited=0;
        while(!dut.ext_cpu_enable&&waited<256)begin @(negedge clk);waited++;end
        check(waited<256,"flash write native slot");
        probe_we=0;probe_addr=16'h0400;
        repeat(64)@(negedge clk);
    endtask
    task automatic flash_wait(input string why);
        integer waited;
        waited=0;
        while(dut.ef_flash_i.engine!=0&&waited<4000000)begin @(negedge clk);waited++;end
        check(waited<4000000,why);
        repeat(64)@(negedge clk);
    endtask
    task automatic flash_command(input [15:0] base,input [7:0] command);
        flash_write(base|16'h0555,8'haa);flash_write(base|16'h02aa,8'h55);
        flash_write(base|16'h0555,command);
    endtask
    // VICE easyflash_memconfig: with M=0 the jumper drives GAME.
    function automatic [1:0] ef_lines(input [2:0] mode,input bit disabled);
        if(!mode[2])ef_lines=disabled ? (mode[1] ? 2'b01 : 2'b11) : (mode[1] ? 2'b00 : 2'b10);
        else ef_lines=mode[1:0]==0 ? 2'b11 : mode[1:0]==1 ? 2'b10 : mode[1:0]==2 ? 2'b01 : 2'b00;
    endfunction
    task automatic easyflash;
        integer total,entries,waited;reg [1:0] lines;bit uci_expected;
        probe_addr=16'h0400;probe_we=0;total=64+128*8208;
        bulk_load=1;
        for(integer a=0;a<total;a++)send(8'h11,a,total,easyflash_byte(a));
        bulk_load=0;repeat(512)@(negedge clk);
        check(dut.cart_loaded&&!load_error&&dut.loader_cart_id==32,"EasyFlash attached");
        check(dut.loader_cart_expansion_policy==2,"EasyFlash allows only UCI");
        active_state(2,"EasyFlash keeps UCI and suppresses REU and sampler");
        check({dut.cart_exrom,dut.cart_game}==2'b10,"EasyFlash boots in Ultimax mode");
        read_value(16'h8000,8'h20,"EasyFlash bank 0 ROML");
        read_value(16'he000,8'h21,"EasyFlash bank 0 ROMH in Ultimax");
        native_write(16'hde00,8'h05);
        read_value(16'h8000,8'h2a,"EasyFlash bank 5 ROML");
        read_value(16'he000,8'h2b,"EasyFlash bank 5 ROMH");
        native_write(16'hde00,8'h3f);read_value(16'he000,8'h9f,"EasyFlash top bank");
        native_write(16'hde00,8'h05);
        // VICE easyflash_memconfig with the jumper in its boot position.
        for(integer mode=0;mode<8;mode++)begin
            native_write(16'hde02,8'h80|mode);
            lines=ef_lines(mode,0);
            check({dut.cart_exrom,dut.cart_game}==lines,"EasyFlash $DE02 memory configuration");
            check(dut.drive_status[10],"EasyFlash LED bit reaches drive status");
            if(lines==2'b00)read_value(16'ha000,8'h2b,"EasyFlash 16K ROMH");
            if(lines==2'b01)read_value(16'ha000,8'hea,"EasyFlash 8K exposes BASIC");
        end
        native_write(16'hde02,8'h07);
        check(!dut.drive_status[10],"EasyFlash LED clears with bit 7");
        // 256 bytes of RAM at $DF00, independent of the bank.
        native_write(16'hdf10,8'h5a);native_write(16'hdfff,8'ha5);
        read_value(16'hdf10,8'h5a,"EasyFlash RAM low byte");
        read_value(16'hdfff,8'ha5,"EasyFlash RAM top byte");
        native_write(16'hde00,8'h00);read_value(16'hdf10,8'h5a,"EasyFlash RAM ignores bank");
        // UCI moves to Ultimate's I/O1 slot $DE18: registers $DE1B-$DE1F.
        read_value(16'hde1d,8'hc9,"EasyFlash UCI identification at $DE1D");
        native_write(16'hdf1d,8'h77);read_value(16'hdf1d,8'h77,"EasyFlash RAM keeps $DF1D");
        native_write(16'hde1e,8'h00);
        check({dut.cart_exrom,dut.cart_game}==2'b00,"EasyFlash UCI masks the $DE1E mode mirror");
        native_write(16'hde1a,8'h00);
        check({dut.cart_exrom,dut.cart_game}==2'b10,"EasyFlash $DE1A remains a mode mirror");
        native_write(16'hde02,8'h07);
        for(integer offset=8'h10;offset<8'h28;offset++)begin
            @(negedge clk);probe_addr=16'hde00+offset;probe_we=0;
            waited=0;while(!dut.ext_cpu_enable&&waited<256)begin @(negedge clk);waited++;end
            check(waited<256,"EasyFlash UCI aperture native slot");#1;
            uci_expected=offset>=8'h1b&&offset<=8'h1f;
            check(dut.uci_io_select==uci_expected,"EasyFlash UCI exact I/O1 aperture");
            check(dut.cartridge_i.IOE==!uci_expected,"EasyFlash UCI masks mapper IO1");
        end
        @(negedge clk);probe_addr=16'h0400;
        // Flash commands through the CPU in Ultimax mode.
        native_write(16'hde02,8'h05);native_write(16'hde00,8'h03);
        flash_command(16'h8000,8'h90);
        read_value(16'h8000,8'h01,"EasyFlash autoselect manufacturer");
        read_value(16'h8001,8'ha4,"EasyFlash autoselect device");
        native_write(16'h8000,8'hf0);read_value(16'h8000,8'h26,"EasyFlash reset to array");
        entries=ef_entries;
        flash_command(16'h8000,8'ha0);flash_write(16'h8123,8'h0f);
        read_value(16'h8123,8'h06,"EasyFlash program ANDs into ROML");
        flash_command(16'he000,8'ha0);flash_write(16'hfffe,8'hf0);
        read_value(16'hfffe,8'h20,"EasyFlash program ANDs into ROMH");
        flash_command(16'h8000,8'h80);flash_command(16'h8000,8'h30);
        // A 64 KiB erase outlasts read_value's window, and reads meanwhile
        // return DQ3/DQ6 status. Software polls until the chip is done.
        flash_wait("EasyFlash sector erase completes");
        read_value(16'h8000,8'hff,"EasyFlash sector erase");
        read_value(16'he000,8'h27,"EasyFlash sector erase spares ROMH");
        native_write(16'hde00,8'h07);read_value(16'h8000,8'hff,"EasyFlash sector covers banks 0-7");
        native_write(16'hde00,8'h08);read_value(16'h8000,8'h30,"EasyFlash next sector intact");
        check(ef_entries==entries+3,"EasyFlash journal records each operation");
        check(ef_journal[entries]=={2'd1,1'b0,6'd3,13'h0123,1'b0,8'h0f} &&
              ef_journal[entries+1]=={2'd1,1'b1,6'd3,13'h1ffe,1'b0,8'hf0} &&
              ef_journal[entries+2]=={2'd2,1'b0,6'd3,13'h0555,1'b0,8'hff},
              "EasyFlash journal entries match the CPU commands");
        // Writes outside Ultimax reach C64 RAM, never the flash.
        native_write(16'hde02,8'h07);entries=ef_entries;
        flash_command(16'h8000,8'ha0);flash_write(16'h8010,8'h00);
        native_write(16'hde02,8'h05);native_write(16'hde00,8'h08);
        read_value(16'h8010,8'h30,"EasyFlash ignores non-Ultimax commands");
        check(ef_entries==entries,"no journal entry outside Ultimax");
        // Jumper Disable: reset hides the cartridge; M=0 GAME follows it.
        ef_boot_disable=1;warm_reset();
        check({dut.cart_exrom,dut.cart_game}==2'b11,"EasyFlash jumper Disable hides the cartridge");
        read_value(16'he000,8'h4c,"EasyFlash jumper Disable boots the KERNAL");
        for(integer mode=0;mode<8;mode++)begin
            native_write(16'hde02,mode);
            check({dut.cart_exrom,dut.cart_game}==ef_lines(mode,1),"EasyFlash jumper Disable memory configuration");
        end
        ef_boot_disable=0;warm_reset();
        check({dut.cart_exrom,dut.cart_game}==2'b10,"EasyFlash reset returns to Ultimax");
        read_value(16'he000,8'h21,"EasyFlash reset selects bank 0");
        native_write(16'hde02,8'h05);native_write(16'hde00,8'h03);
        read_value(16'h8123,8'hff,"EasyFlash flash survives warm reset");
        send(8'h14,0,1,0);repeat(128)@(negedge clk);
        check(!dut.cart_loaded,"EasyFlash detached");
        active_state(7,"detach restores requested expansions");
        $display("EASYFLASH PASS NTSC=%0d modes=16 journal=%0d uci=DE1B",NTSC,ef_entries);
    endtask
    task automatic normal_layouts;
        attach_normal(0);
        read_value(16'h8000,8'h90,"normal 8K low ROM");
        read_value(16'h9fff,8'h90,"normal 8K low ROM end");
        read_value(16'ha000,8'hea,"normal 8K exposes BASIC");
        native_write(16'h8002,8'h5c);read_value(16'h8002,8'h90,"normal 8K ROM write leaves ROM");
        attach_normal(1);
        read_value(16'h8000,8'h92,"normal 16K one-packet low half");
        read_value(16'ha000,8'h93,"normal 16K one-packet high half");
        attach_normal(2);
        read_value(16'h8000,8'h94,"normal 16K first packet");
        read_value(16'hbfff,8'h95,"normal 16K second packet");
        attach_normal(3);
        read_value(16'he000,8'h96,"normal Ultimax high ROM");
        read_value(16'hffff,8'h96,"normal Ultimax high ROM end");
        native_write(16'h8002,8'h3c);
        warm_reset();read_value(16'he000,8'h96,"normal Ultimax after reset");
        send(8'h14,0,1,0);repeat(128)@(negedge clk);
        check(!dut.cart_loaded,"normal cartridge detached");
        read_value(16'h8002,8'h5c,"8K write reached RAM and Ultimax ROML write did not");
        $display("NORMAL_LAYOUT PASS NTSC=%0d layouts=4",NTSC);
    endtask
    task automatic apertures;
        integer waited;bit reu_expected,uci_expected;
        for(integer writing=0;writing<2;writing++)for(integer offset=0;offset<256;offset++)begin
            @(negedge clk);probe_addr=16'hdf00+offset;probe_we=writing;probe_data=0;
            waited=0;while(!dut.ext_cpu_enable&&waited<256)begin @(negedge clk);waited++;end
            check(waited<256,"aperture native slot");#1;
            uci_expected=offset>=27&&offset<=31;
            reu_expected=(writing?offset<32:offset<24)&&!uci_expected;
            check(dut.reu_io_select==reu_expected,"FC3 narrow REU aperture");
            check(dut.uci_io_select==uci_expected,"FC3 exact UCI aperture");
            check(!dut.ua_io_select,"FC3 sampler remains suppressed");
            check(dut.cartridge_i.IOF==!(reu_expected||uci_expected),"expansion masks mapper IO2");
        end
        @(negedge clk);probe_addr=16'h0400;probe_we=0;
    endtask
    task automatic transfer(input integer mode);
        integer initial_bytes,waited;bit stash;
        // All control writes go through the native CPU pins, PLA and exact
        // expansion aperture while FC3 remains attached and mapped.
        stash=mode==1||mode==2;
        native_write(16'hdf02,0);native_write(16'hdf03,mode>=2?8'h80:stash?8'h31:8'h30);
        native_write(16'hdf04,0);native_write(16'hdf05,stash?1:0);
        native_write(16'hdf06,0);native_write(16'hdf07,64);
        native_write(16'hdf08,0);native_write(16'hdf09,0);native_write(16'hdf0a,0);
        initial_bytes=dma_bytes;native_write(16'hdf01,stash?8'h90:8'h91);
        waited=0;while((dma_bytes-initial_bytes<64||dut.reu_memory_busy)&&waited<100000)begin
            @(negedge clk);waited++;
        end
        check(waited<100000&&dma_bytes-initial_bytes==64,"FC3 bidirectional REU DMA completion");
        for(integer a=0;a<64;a++)begin
            if(mode==1)check(backing[256+a]===8'h60+a,"FC3 stash exact DDR bytes");
            else if(mode==2)check(backing[256+a]===8'h42,"FC3 mapped ROM stash exact DDR bytes");
            else if(mode==3)check(dut.main_ram_i.main_high[a]===8'h80+a,"FC3 fetch writes RAM underneath ROM");
            else check(dut.main_ram_i.main_low[16'h3000+a]===8'h80+a,"FC3 fetch exact C64 bytes");
        end
        check(dut.cart_loaded&&dut.loader_cart_id==3,"DMA preserves attached FC3");
        $display("CART_DMA PASS NTSC=%0d mode=%0d bytes=64 requests=%0d",NTSC,mode,requests);
    endtask
    initial begin
        for(integer a=0;a<512;a++)backing[a]=8'h80+(a&63);
        for(integer a=0;a<2048;a++)buffer[a]=0;
        for(integer a=0;a<8192;a++)rom[a]=8'hea;
        rom[0]=8'h4c;rom[1]=0;rom[2]=8'he0;
        rom[8190]=0;rom[8191]=8'he0;rom[8188]=0;rom[8189]=8'he0;rom[8186]=0;rom[8187]=8'he0;
        repeat(8)@(negedge clk);reset=0;
        for(integer kind=1;kind<=3;kind++)for(integer a=0;a<(kind==3?4096:8192);a++)
            send(kind,a,kind==3?4096:8192,kind==2?rom[a]:8'hea);
        for(integer a=0;a<32768;a++)begin dut.main_ram_i.main_low[a]=8'h5a;dut.main_ram_i.main_high[a]=8'h5a;end
        for(integer a=0;a<1024;a++)dut.main_ram_i.colour[a]=4'h5;
        pause_request=0;repeat(20000)@(negedge clk);
        force dut.cpu_island_i.cpu_enable=0;
        force dut.ext_cpu_addr=probe_addr;force dut.ext_cpu_we=probe_we;
        force dut.ext_cpu_do=probe_data;force dut.ext_cpu_io=8'h37;
        video_idle();active_state(7,"no cartridge retains requested expansions");
        // The upstream VIC refresh counter seeds itself at the end of its
        // first frame. Repeated loader resets must not preempt that frame in
        // this synthetic KERNAL fixture before testing Ultimax VIC accesses.
        repeat(NTSC?600000:700000)@(negedge clk);
        // EasyFlash negative controls run that scenario alone.
        if($test$plusargs("easyflash_only"))begin
            easyflash();$display("EASYFLASH ONLY NTSC=%0d checks=%0d",NTSC,checks);$finish;
        end
        attach(3,0,4,16384,0);active_state(0,"ordinary FC3 suppresses conflicting expansions");
        read_value(16'h8000,8'h40,"FC3 bank zero ROM read");
        for(integer bank=1;bank<4;bank++)begin
            native_write(16'hdfff,8'h40+bank);
            read_value(16'h8000,8'h40+bank,"FC3 DFFF bank switching");
            read_value(16'ha000,8'h40+bank,"FC3 high ROM bank switching");
        end
        attach(3,1,4,16384,3);active_state(3,"REU-compatible FC3 preserves REU/UCI");
        read_value(16'hdf1d,8'hc9,"FC3 UCI identification visible");
        native_write(16'hdf02,8'h5c);read_value(16'hdf02,8'h5c,"FC3 REU native register readback");
        apertures();
        // Aperture writes include DFFF, so reset the guest before checking
        // banking and DMA. Host requested expansion flags remain unchanged.
        guest_reset=1;repeat(32)@(negedge clk);guest_reset=0;
        repeat(512)@(negedge clk);active_state(3,"guest reset retains FC3 policy");
        video_idle();
        for(integer a=0;a<64;a++)dut.main_ram_i.main_low[16'h3100+a]=8'h60+a;
        transfer(0);transfer(1);
        native_write(16'hdfff,8'h42);read_value(16'h8000,8'h42,"REU enabled preserves DFFF bank control");
        transfer(2);transfer(3);
        read_value(16'h8000,8'h42,"REU write underneath ROM preserves mapped ROM");
        native_write(16'hdfff,8'hf0);read_value(16'h8000,8'h80,"FC3 DFFF disable exposes REU-written RAM");
        active_state(3,"software-disabled FC3 retains subtype policy");
        send(8'h14,0,1,0);repeat(128)@(negedge clk);
        check(!dut.cart_loaded,"detach removes cartridge");active_state(7,"detach restores all requested expansions");
        attach(19,0,1,8192,7);active_state(7,"IO1 Magic Desk permits all expansions");
        read_value(16'h8000,8'h40,"Magic Desk ROM mapped");
        read_value(16'hdf1d,8'hc9,"Magic Desk UCI remains visible");
        native_write(16'hde00,8'h80);read_value(16'h8000,8'h80,"Magic Desk IO1 disable retained");
        twomegabyter();
        rom_simple();blackboxes();megabyter();
        // The 2 MiB streams test the loader and mapper, which do not depend
        // on the video standard, so the runner skips them on the second one.
        if(!$test$plusargs("skip_capacity"))upper_banks();
        normal_layouts();easyflash();
        attach(10,0,1,8192,0);active_state(0,"IO2 Epyx suppresses conflicting expansions");
        attach(3,1,4,16384,3);active_state(3,"replacement restores compatible expansions");
        for(integer a=0;a<128;a++)send(8'h11,a,65664,crt_byte(a,3,0,4,16384));
        check(dut.cart_loading&&!dut.reu_active&&!dut.ua_active&&!dut.uci_active,"partial replacement suppresses expansions");
        load_abort=1;repeat(8)@(negedge clk);load_abort=0;
        repeat(128)@(negedge clk);clear_load_error=1;repeat(4)@(negedge clk);clear_load_error=0;
        repeat(128)@(negedge clk);
        check(!dut.cart_loaded&&!dut.cart_loading&&!load_error,"aborted replacement discards partial cartridge");
        active_state(7,"abort restores requested expansions after clear");
        check(cart_reads>0&&requests>0&&dma_bytes==272,"real memory paths exercised");
        $display("PASS CART COEXIST NTSC=%0d profiles=%0d checks=%0d DMA=%0d cartreads=%0d",NTSC,profiles,checks,dma_bytes,cart_reads);$finish;
    end
    initial begin #2000000000;$fatal(1,"CART COEXIST global timeout");end
endmodule

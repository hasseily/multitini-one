// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Full adapter: actual T65/island/proxy, UCI, VIC/CIA, REU and REU cache.
// Only ROM contents, host UCI buffer/operations and DDR responses are fixtures.
module tb_c64_uci_machine #(parameter integer NTSC=0);
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
    multitini_c64_adapter #(.NTSC_MODE(NTSC),.CORE_CLOCK_HZ(32000000),
        .ENABLE_EXTENSIONS(1),.SID_COUNT(8)) dut(
        .core_clk(clk),.cpu_clk(fastclk),.reset(reset),.core_reset_request(1'b0),
        .cpu_config(config_word),.cpu_status(cpu_status),.cpu_turbo_enable(1'b0),
        .ua_enable(1'b0),.debug_enable(1'b0),.uci_enable(1'b1),.uci_control(control),
        .uci_op(op),.uci_op_valid(op_valid),.uci_op_ready(),.uci_ram_request(32'd0),
        .uci_status(uci_status),.uci_status2(uci_status2),
        .uci_buffer_addr(buffer_addr),.uci_buffer_write(buffer_we),
        .uci_buffer_wdata(buffer_wdata),.uci_buffer_rdata(buffer_rdata),
        .sid_map_enable(1'b0),.sid_slot_masks(256'd0),.sid_params(256'd0),
        .sid_write_inhibit(32'd0),.sid_output_config(32'd0),.sid_owners(96'd0),.sid_io_masks(128'd0),
        .peek_word(32'd0),.reu_host_word(32'd0),
        .ua_memory_ready(1'b1),.ua_memory_rvalid(1'b0),.ua_memory_fault(1'b0),.ua_memory_rdata(64'd0),
        .iec9_run(1'b0),.iec9_quiesce(1'b0),.iec9_event_ready(1'b1),
        .iec9_reply_data(32'd0),.iec9_reply_valid(1'b0),
        .pause_request(pause_request),.drive_parallel_enable(1'b0),.sid_mode(3'd0),.reu_enable(1'b1),
        .load_abort(1'b0),.load_abort_epoch(1'b0),.clear_load_error(1'b0),
        .input_code(16'd0),.input_value(16'd0),.input_valid(1'b0),
        .load_data(load_data),.load_valid(load_valid),.load_ready(load_ready),
        .load_first(load_first),.load_last(load_last),.load_kind(load_kind),
        .load_offset(load_offset),.load_total(load_total),.load_epoch(1'b0),
        .memory_ready(1'b1),.memory_rdata(64'd0),.memory_rvalid(1'b0),.memory_fault(1'b0),
        .reu_memory_addr(ddr_addr),.reu_memory_write(ddr_write),.reu_memory_wdata(ddr_wdata),
        .reu_memory_wstrb(ddr_wstrb),.reu_memory_valid(ddr_valid),.reu_memory_burst(ddr_burst),
        .reu_memory_busy(ddr_busy),.reu_memory_ready(ddr_ready),.reu_memory_rdata(ddr_rdata),
        .reu_memory_rvalid(ddr_rvalid),.reu_memory_fault(1'b0),
        .media_mount(1'b0),.media_eject(1'b0),.media_complete(1'b0),.media_slot(8'd0),
        .media_kind(8'd0),.media_read_only(1'b1),.media_image_size(32'd0),
        .media_generation(32'd0),.media_complete_request_id(32'd0),.media_complete_status(32'd0),
        .media_complete_block_count(16'd0),.media_complete_generation(32'd0),.media_write_data_ready(1'b1),
        .ready(ready),.load_error(load_error));
    // The production two-second startup guard is unrelated to these seams.
    // Shorten only that counter, not a CPU/native scheduling parameter.
    defparam dut.turbo_regs_i.RESET_WINDOW_CYCLES=256;

    always @(posedge clk) begin
        if(buffer_we)buffer[buffer_addr]<=buffer_wdata;
        buffer_rdata<=buffer[buffer_addr];
        ddr_rvalid<=0;
        if(reset)begin pending<=0;requests<=0;end
        else begin
            if(ddr_valid&&ddr_ready)begin
                if(ddr_addr>480)$fatal(1,"UCI machine backing-store range");
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
        end
    end

    integer mode=0,stash=0,cases=0,rows,bus_id_reads=0;
    integer retires=0,irq_entries=0,irq_clears=0,irq_release_checks=0,dma_bytes=0;
    integer copied=0,request_writes=0,hold_dma_bytes=0,read_clears=0,control_clears=0,target_requests=0;
    integer ff00_writes=0,trigger_progress=0,trigger_dma_bytes=0;
    reg [15:0] pending_ack_addr=0;
    reg measuring=0,finished=0,reply_pending=0;
    wire cpu_enable=dut.cpu_island_i.cpu_enable;
    wire cpu_write=dut.cpu_island_i.cpu_write;
    wire [15:0] cpu_addr=dut.cpu_island_i.cpu_address;
    wire [7:0] cpu_data=dut.cpu_island_i.cpu_wdata;
    always @(posedge fastclk) if(measuring&&cpu_enable)begin
        retires++;
        if(cpu_write&&cpu_addr==16'h2106)begin
            if(cpu_data!=={3'd0,control[4:0]})$fatal(1,"DF1B real CPU bus ID readback");
            bus_id_reads++;
        end
        if(reply_pending && cpu_addr!=pending_ack_addr)begin
            if(dut.uci_irq!==0 || (mode!=0&&dut.cpu_island_i.irq_q!==dut.ext_irq_n))
                $fatal(1,"UCI IRQ clear not visible before next CPU enable");
            irq_release_checks++;reply_pending=0;
        end
        if(cpu_write&&cpu_addr==16'h2100&&request_writes!=0&&cpu_data!=8'h5a)$fatal(1,"UCI reply byte did not reach CPU");
        if(cpu_write&&cpu_addr==16'h2110&&cpu_data!=8'h49)$fatal(1,"DF1D must report pending IRQ");
        if(cpu_write&&cpu_addr==16'h2111&&cpu_data!=8'hc9)$fatal(1,"DF1D must report inactive IRQ after clear");
        if(cpu_write&&cpu_addr==16'h2101&&cpu_data!=0)irq_entries++;
        if(cpu_write&&cpu_addr==16'h2102&&cpu_data==8'ha6)begin
            if(!uci_status[6]||dut.uci_freeze)$fatal(1,"UCI trigger froze CPU before FF00");
            trigger_progress++;
        end
        if(cpu_write&&cpu_addr>=16'h2200&&cpu_addr<16'h2240)begin
            if(cpu_addr!=16'h2200+copied || cpu_data!=(stash?8'h40:8'h80)+copied)
                $fatal(1,"UCI DMA data readback index=%0d address=%h data=%h",copied,cpu_addr,cpu_data);
            copied++;
        end
        if(cpu_write&&cpu_addr==16'h2105&&cpu_data==8'ha5)finished=1;
    end
    always @(posedge clk) if(measuring)begin
        if(dut.cpu_bus_valid&&dut.cpu_bus_write&&dut.cpu_bus_addr==16'hdf1c)begin
            if(dut.cpu_bus_wdata==8'ha1||dut.cpu_bus_wdata==8'h21||dut.cpu_bus_wdata==8'h61)request_writes++;
            if(dut.cpu_bus_wdata==2)begin
                irq_clears++;
                if(dut.uci_irq)begin reply_pending=1;pending_ack_addr=16'hdf1c;control_clears++;end
            end
        end
        if(dut.cpu_bus_valid&&!dut.cpu_bus_write&&dut.uci_irq&&
            (dut.cpu_bus_addr==16'hdf1e||dut.cpu_bus_addr==16'hdf1f))begin
            read_clears++;reply_pending=1;pending_ack_addr=dut.cpu_bus_addr;
        end
        if(dut.cpu_bus_valid&&dut.cpu_bus_write&&dut.cpu_bus_addr==16'hff00)begin
            if(!uci_status[6]||trigger_progress!=1)$fatal(1,"Accepted FF00 did not follow armed UCI trigger and CPU progress");
            ff00_writes++;
        end
        if(dut.uci_freeze&&dut.reu_busy&&dut.core_paused)$fatal(1,"UCI freeze paused active REU");
        if(dut.reu_dma_ack)begin
            if(dut.reu_dma_addr!=(stash?16'h3100:16'h3000)+(dma_bytes%64) ||
               dut.reu_dma_write!==!stash ||
               (stash?dut.reu_dma_rdata:dut.reu_dma_wdata)!=(stash?8'h40:8'h80)+(dma_bytes%64))
                $fatal(1,"UCI active DMA trace differs byte=%0d address=%h",dma_bytes,dut.reu_dma_addr);
            dma_bytes++;if(control[10])hold_dma_bytes++;
            if(dut.uci_freeze)trigger_dma_bytes++;
        end
    end
    task automatic load_rom(input [7:0] kind,input integer size);
        @(negedge clk);load_kind=kind;load_total=size;load_valid=1;
        for(integer a=0;a<size;a++)begin
            load_offset=a;load_first=a==0;load_last=a==size-1;
            load_data=kind==2?rom[a]:kind==3?8'hff:8'hea;
            do begin @(posedge clk);#1;end while(!load_ready);
            @(negedge clk);
        end
        load_valid=0;load_first=0;load_last=0;repeat(8)@(negedge clk);
    endtask
    task automatic wait_count(input integer selector,input integer limit);
        integer elapsed;bit done;
        begin
            elapsed=0;done=0;
            while(!done&&elapsed<limit)begin
                @(negedge clk);elapsed++;
                case(selector)
                    0:done=uci_status[1:0]==1&&dut.uci_freeze&&dut.core_paused;
                    1:done=dut.reu_busy&&dma_bytes>=68;
                    2:done=!dut.reu_busy&&!ddr_busy&&dut.core_paused;
                    3:done=finished;
                    4:done=request_writes==target_requests&&uci_status[1:0]==1;
                    5:done=dut.uci_freeze&&dut.core_paused&&!ddr_busy&&dma_bytes==64;
                endcase
                if(load_error)$fatal(1,"UCI machine loader/cache error");
            end
            if(!done)$fatal(1,"UCI machine timeout selector=%0d NTSC=%0d mode=%0d stash=%0d CPU=%h status=%h dma=%0d busy=%b paused=%b",selector,NTSC,mode,stash,cpu_addr,uci_status,dma_bytes,dut.reu_busy,dut.core_paused);
        end
    endtask
    task automatic decode_sweep;
        reg [3:0] prior_epoch;
        begin
            // Isolated adapter decode proof. CPU and DMA are paused; forced
            // selection inputs do not stand in for a loaded CRT execution.
            pause_request=1;repeat(128)@(negedge clk);
            if(!dut.core_paused)$fatal(1,"Decode sweep machine not paused");
            force dut.cpu_bus_valid=0;force dut.cpu_bus_addr=16'hdf1c;
            force dut.cart_iof=1;force dut.reu_dma_req=0;
            force dut.cart_loaded=0;force dut.cart_loading=0;force dut.loader_cart_id=0;
            repeat(4)@(negedge clk);
            if(!dut.uci_active||!dut.uci_io_select||dut.uci_suspended)$fatal(1,"UCI no-cart decode");
            prior_epoch=uci_status[25:22];
            force dut.cart_loaded=1;repeat(4)@(negedge clk);
            if(!dut.uci_active||!dut.uci_io_select||dut.uci_suspended||dut.cart_io_read||uci_status[25:22]!=prior_epoch)
                $fatal(1,"UCI generic-cart IO2 ownership");
            force dut.cpu_bus_addr=16'hdf20;#1;if(dut.uci_io_select)$fatal(1,"UCI escaped DF18..DF1F");
            force dut.cpu_bus_addr=16'hdf18;#1;if(!dut.uci_io_select)$fatal(1,"UCI DF18 boundary missing");
            force dut.reu_dma_req=1;#1;if(dut.uci_io_select||!dut.uci_active)$fatal(1,"DMA must release UCI IO2 without resetting endpoint");
            force dut.reu_dma_req=0;
            force dut.loader_cart_id=1;repeat(4)@(negedge clk);
            if(dut.uci_active||dut.uci_io_select||!dut.uci_suspended||uci_status[25:22]==prior_epoch)
                $fatal(1,"Non-generic cart must suspend/reset UCI");
            force dut.cart_loaded=0;force dut.loader_cart_id=0;repeat(4)@(negedge clk);
            prior_epoch=uci_status[25:22];force dut.cart_loading=1;repeat(4)@(negedge clk);
            if(dut.uci_active||dut.uci_io_select||!dut.uci_suspended||uci_status[25:22]==prior_epoch)
                $fatal(1,"Cartridge load must suspend/reset UCI");
            release dut.cpu_bus_valid;release dut.cpu_bus_addr;release dut.cart_iof;
            release dut.reu_dma_req;release dut.cart_loaded;release dut.cart_loading;release dut.loader_cart_id;
            $display("UCI_DECODE PASS checks=7 NTSC=%0d",NTSC);
        end
    endtask
    initial begin
        string path;integer held_retires,initial_dma;
        rows=$fopen("uci_machine.csv","w");
        $fdisplay(rows,"ntsc,mode,stash,requests,irq_clears,read_clears,control_clears,irq_release_checks,ff00_writes,trigger_dma_bytes,dma_bytes,hold_dma_bytes,copied");
        for(integer speed=0;speed<3;speed++)for(integer direction=0;direction<2;direction++)begin
            measuring=0;reset=1;pause_request=1;control=11'h31f;mode=speed;stash=direction;
            config_word=mode==0?0:mode==1?32'h45f:32'h41f;
            control=11'h300|(mode==0?11'd31:11'd9);bus_id_reads=0;
            path=$sformatf("kernal-%0d.hex",stash);$readmemh(path,rom);
            for(integer a=0;a<512;a++)backing[a]=8'h80+(a&63);
            for(integer a=0;a<2048;a++)buffer[a]=0;
            repeat(8)@(negedge clk);reset=0;
            load_rom(1,8192);load_rom(3,4096);load_rom(2,8192);
            repeat(32)@(negedge clk);if(!ready)$fatal(1,"Synthetic ROM boot not ready");
            retires=0;irq_entries=0;irq_clears=0;irq_release_checks=0;dma_bytes=0;
            copied=0;request_writes=0;hold_dma_bytes=0;finished=0;reply_pending=0;
            read_clears=0;control_clears=0;target_requests=0;
            ff00_writes=0;trigger_progress=0;trigger_dma_bytes=0;
            measuring=1;pause_request=0;
            wait_count(0,200000);
            if(request_writes!=1||uci_status[17:8]!=2||buffer[0]!=4||buffer[1]!=1)
                $fatal(1,"Real CPU request did not reach UCI BRAM once");
            if(dut.island_turbo!==(mode!=0))$fatal(1,"Requested CPU mode not active at PUSH");
            held_retires=retires;repeat(160)@(negedge clk);
            if(retires!=held_retires)$fatal(1,"Freeze allowed CPU progress after pause acknowledgement");
            buffer['h380]=8'h5a;
            op={uci_status[24:22],uci_status[21:18],1'b0,8'd0,2'd0,10'd1,4'd4};
            op_valid=1;@(negedge clk);op_valid=0;
            for(integer stage=2;stage<=3;stage++)begin
                target_requests=stage;wait_count(4,100000);
                if(uci_status[17:8]!=2||buffer[0]!=4||buffer[1]!=1)$fatal(1,"Repeated CPU UCI command bytes");
                buffer['h700]=8'h5a;
                op={uci_status[24:22],uci_status[21:18],1'b0,stage==2?8'd1:8'd0,2'd0,10'd0,4'd4};
                op_valid=1;@(negedge clk);op_valid=0;
            end
            target_requests=4;wait_count(4,900000);wait_count(5,100000);
            if(ff00_writes!=1||trigger_progress!=1||trigger_dma_bytes!=64||uci_status[6])
                $fatal(1,"Simultaneous FF00 UCI trigger/REU transfer was not completed");
            held_retires=retires;repeat(160)@(negedge clk);
            if(retires!=held_retires)$fatal(1,"Triggered freeze allowed CPU progress after DMA/pause acknowledgement");
            op={uci_status[24:22],uci_status[21:18],1'b0,8'd0,2'd0,10'd0,4'd4};
            op_valid=1;@(negedge clk);op_valid=0;
            wait_count(1,900000);
            control[10]=1;initial_dma=dma_bytes;
            wait_count(2,200000);
            if(dma_bytes!=128||hold_dma_bytes!=128-initial_dma||hold_dma_bytes==0)
                $fatal(1,"Host HOLD stopped active REU instead of draining it");
            if(stash)for(integer a=0;a<64;a++)if(backing[256+a]!=8'h40+a)
                $fatal(1,"Host HOLD acknowledged before posted DDR write drain");
            held_retires=retires;repeat(160)@(negedge clk);
            if(retires!=held_retires)$fatal(1,"Host HOLD allowed CPU progress after pause acknowledgement");
            control[10]=0;wait_count(3,100000);
            repeat(96)@(negedge clk);measuring=0;
            if(bus_id_reads!=1||irq_entries!=4||irq_clears!=4||irq_release_checks!=4||read_clears!=2||control_clears!=2||copied!=64||dut.uci_irq!==0)
                $fatal(1,"Incomplete UCI machine test IRQ=%0d clear=%0d release=%0d copied=%0d",irq_entries,irq_clears,irq_release_checks,copied);
            $fdisplay(rows,"%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d",NTSC,mode,stash,requests,irq_clears,read_clears,control_clears,irq_release_checks,ff00_writes,trigger_dma_bytes,dma_bytes,hold_dma_bytes,copied);$fflush(rows);
            $display("UCI_MACHINE_CASE NTSC=%0d mode=%0d stash=%0d requests=%0d DMA=%0d HOLD_DMA=%0d",NTSC,mode,stash,requests,dma_bytes,hold_dma_bytes);
            cases++;
        end
        decode_sweep();
        $fclose(rows);$display("PASS UCI machine cases=%0d NTSC=%0d",cases,NTSC);$finish;
    end
    initial begin #1000000000;$fatal(1,"UCI machine global timeout");end
endmodule

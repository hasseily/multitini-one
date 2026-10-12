// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// Real shell framing and reset/abort crossings, with physical-write completion
// deliberately later than STREAM_STATUS_DRAINED. No chipset or PSRAM PHY model.
module tb_genesis_stream_loader #(
    parameter bit ACTUAL_ADAPTER=0
);
    import multitini_regs_pkg::*;
    import multitini_genesis_defs_pkg::*;
    reg control_clk=0,core_clk=0,control_resetn=0,core_resetn=0;
    always #3.750 control_clk=~control_clk;
    always #4.655 core_clk=~core_clk;
    reg wr_en=0;
    reg [11:0] wr_addr=0,rd_addr=0;
    reg [31:0] wr_data=0;
    reg [3:0] wr_strb=0;
    wire [31:0] rd_data,control;
    wire load_submit,load_submit_ready,load_first,load_last,load_epoch;
    wire [7:0] load_kind,load_data,load_abort_kind;
    wire [31:0] load_offset,load_total;
    wire load_abort,load_abort_epoch,load_error_clear;
    wire [82:0] payload;
    wire dst_valid,dst_ready,dst_first,dst_last,dst_epoch;
    wire [7:0] dst_kind,dst_data;
    wire [31:0] dst_offset,dst_total;
    wire reset_request,clear_error,dst_abort,abort_ready;
    reg abort_pending=0,abort_pending_epoch=0;
    reg [7:0] abort_pending_kind=0;
    wire [8:0] abort_payload;
    wire error,cart_loaded,config_valid,cart_download,cart_word_write;
    wire [31:0] cart_size;
    wire [22:0] cart_word_address;
    wire [15:0] cart_word_data,save_address;
    wire [7:0] save_data;
    wire save_write,write_valid,write_ready;
    wire [23:0] write_addr;
    wire [63:0] write_data;
    wire [7:0] write_strobe;
    wire write_response;
    reg model_write_response=0;
    integer physical_wait=0,core_ticks=0,round_id=0;
    integer cart_writes=0,cart_words=0,save_writes=0,overlap_cycles=0;
    integer completions=0,abort_drains=0,rounds=0;
    reg stress_abort=0,monitor_save_overlap=0;
    reg [23:0] physical_addr=0;
    reg [63:0] physical_data=0;
    reg [7:0] physical_strobe=0;
    localparam integer CART_BYTES=ACTUAL_ADAPTER ? 1024 : 512;
    reg [7:0] rom[0:CART_BYTES-1],program_rom[0:1023],save_memory[0:65535];
    reg ready_launch=0,error_launch=0;
    wire ready_control,error_control;
    wire modeled_ready,stale_byte,boot_signature;

    multitini_control_regs #(.SHELL_BUILD_ID(32'h50484f34)) regs_i(
        .clk(control_clk),.resetn(control_resetn),.core_id(multitini_genesis_defs_pkg::MT_GENESIS_CORE_ID),.core_variant_index(32'd0),
        .core_ready(ready_control),.core_load_error(error_control),.load_submit_ready(load_submit_ready),
        .vectrex_overlay_status(2'd0),.input_submit_ready(1'b1),
        .media_request_valid(1'b0),.media_request_write(1'b0),.media_request_id(32'd0),
        .media_request_lba(32'd0),.media_request_block_count(16'd0),.media_request_generation(32'd0),
        .media_data_valid(1'b0),.media_data(8'd0),.media_data_last(1'b0),
        .media_state_mounted(1'b0),.media_state_dirty(1'b0),.media_state_eject_pending(1'b0),.media_submit_ready(1'b1),
        .ui_active_visible(1'b0),.ui_active_bank(1'b0),.ui_active_dense(1'b0),.ui_submit_ready(1'b1),
        .effect_busy(1'b0),.effect_active_valid(1'b0),.effect_rejected(1'b0),.effect_active_sequence(32'd0),
        .wr_en(wr_en),.wr_addr(wr_addr),.wr_data(wr_data),.wr_strb(wr_strb),.rd_en(1'b0),.rd_addr(rd_addr),.rd_data(rd_data),
        .control(control),.border_config(),.vectrex_video_config(),
        .effect_write(),.effect_commit(),.effect_address(),.effect_data(),.effect_commit_sequence(),
        .input_submit(),.input_code(),.input_value(),.media_mount(),.media_eject(),.media_complete(),
        .media_slot(),.media_kind(),.media_read_only(),.media_image_size(),.media_generation(),
        .media_complete_request_id(),.media_complete_status(),.media_complete_block_count(),.media_complete_generation(),
        .media_data_pop(),.ui_submit(),.ui_visible(),.ui_bank(),.ui_dense(),.irq(),
        .load_submit(load_submit),.load_kind(load_kind),.load_data(load_data),
        .load_offset(load_offset),.load_total(load_total),.load_first(load_first),.load_last(load_last),.load_epoch(load_epoch),
        .load_abort(load_abort),.load_abort_epoch(load_abort_epoch),.load_abort_kind(load_abort_kind),.load_error_clear(load_error_clear));
    multitini_async_mailbox #(.WIDTH(83)) payload_i(
        .src_clk(control_clk),.src_resetn(control_resetn),
        .src_data({load_kind,load_data,load_offset,load_total,load_first,load_last,load_epoch}),
        .src_valid(load_submit),.src_ready(load_submit_ready),
        .dst_clk(core_clk),.dst_resetn(core_resetn),.dst_data(payload),.dst_valid(dst_valid),.dst_ready(dst_ready));
    assign {dst_kind,dst_data,dst_offset,dst_total,dst_first,dst_last,dst_epoch}=payload;
    // This is the source event holding logic used by multitini_one_top.
    always @(posedge control_clk) begin
        if(!control_resetn)begin abort_pending<=0;abort_pending_epoch<=0;abort_pending_kind<=0;end
        else if(load_abort)begin abort_pending<=1;abort_pending_epoch<=load_abort_epoch;abort_pending_kind<=load_abort_kind;end
        else if(abort_pending && abort_ready)abort_pending<=0;
    end
    multitini_async_mailbox #(.WIDTH(9)) abort_i(
        .src_clk(control_clk),.src_resetn(control_resetn),.src_data({abort_pending_kind,abort_pending_epoch}),
        .src_valid(abort_pending),.src_ready(abort_ready),
        .dst_clk(core_clk),.dst_resetn(core_resetn),.dst_data(abort_payload),.dst_valid(dst_abort),.dst_ready(1'b1));
    cdc_bit_sync reset_i(.clk(core_clk),.resetn(core_resetn),
        .din(|(control & multitini_regs_pkg::MT_CONTROL_CORE_RESET_REQUEST)),.dout(reset_request));
    multitini_cdc_pulse_toggle clear_i(.src_clk(control_clk),.src_resetn(control_resetn),.src_pulse(load_error_clear),
        .dst_clk(core_clk),.dst_resetn(core_resetn),.dst_pulse(clear_error));
    always @(posedge core_clk)begin
        if(!core_resetn)begin ready_launch<=0;error_launch<=0;end
        else begin ready_launch<=modeled_ready;error_launch<=error;end
    end
    cdc_bit_sync ready_i(.clk(control_clk),.resetn(control_resetn),.din(ready_launch),.dout(ready_control));
    cdc_bit_sync error_i(.clk(control_clk),.resetn(control_resetn),.din(error_launch),.dout(error_control));
    generate if(!ACTUAL_ADAPTER)begin:g_loader
    assign modeled_ready=!reset_request && cart_loaded && config_valid && !error;
    assign boot_signature=1'b1;
    assign stale_byte=loader_i.stale_byte;
    assign write_response=model_write_response;
    assign write_ready=physical_wait==0 && !write_response && core_ticks%5!=0;
    multitini_genesis_loader loader_i(
        .clk(core_clk),.reset(!core_resetn),.core_reset_request(reset_request),.clear_error(clear_error),
        .load_abort(dst_abort),.load_abort_epoch(abort_payload[0]),.load_epoch(dst_epoch),
        .load_data(dst_data),.load_kind(dst_kind),.load_valid(dst_valid),.load_first(dst_first),.load_last(dst_last),
        .load_offset(dst_offset),.load_total(dst_total),.load_ready(dst_ready),
        .error(error),.cart_loaded(cart_loaded),.config_valid(config_valid),.cart_size(cart_size),
        .region(),.six_button(),.lowpass(),.fm_chip(),.cache_invalidate(),
        .cart_download(cart_download),.cart_word_write(cart_word_write),.cart_word_address(cart_word_address),.cart_word_data(cart_word_data),
        .save_address(save_address),.save_data(save_data),.save_write(save_write),
        .write_addr(write_addr),.write_data(write_data),.write_strobe(write_strobe),.write_valid(write_valid),.write_ready(write_ready),
        .write_response(write_response),.memory_fault(1'b0));
    end
`ifdef MT_GENESIS_ACTUAL_TEST
    else begin:g_actual
        wire [23:0] memory_addr,service_addr,command_addr;
        wire memory_write,memory_valid,memory_ready,memory_rvalid,memory_fault;
        wire [63:0] memory_wdata,memory_rdata,service_wdata,service_rdata,command_wdata;
        wire [7:0] memory_wstrb,service_wstrb,command;
        wire service_write,service_valid,service_ready,service_rvalid;
        wire command_valid,command_ready,initialized;
        reg driver_rvalid=0;
        reg [63:0] driver_rdata=0,driver_data=0;
        reg [7:0] driver_command=0;
        reg [23:0] driver_address=0;
        integer phy_wait=0;
        reg [63:0] device_lines[0:127];
        multitini_genesis_adapter adapter_i(
            .core_clk(core_clk),.reset(!core_resetn),.core_reset_request(reset_request),
            .pause_request(1'b0),.clear_load_error(clear_error),
            .input_code(16'd0),.input_value(16'd0),.input_valid(1'b0),.input_ready(),
            .load_data(dst_data),.load_kind(dst_kind),.load_valid(dst_valid),.load_ready(dst_ready),
            .load_first(dst_first),.load_last(dst_last),.load_offset(dst_offset),.load_total(dst_total),
            .load_epoch(dst_epoch),.load_abort(dst_abort),.load_abort_epoch(abort_payload[0]),
            .memory_addr(memory_addr),.memory_write(memory_write),.memory_wdata(memory_wdata),.memory_wstrb(memory_wstrb),
            .memory_valid(memory_valid),.memory_ready(memory_ready),.memory_rdata(memory_rdata),.memory_rvalid(memory_rvalid),.memory_fault(memory_fault),
            .media_mount(1'b0),.media_eject(1'b0),.media_complete(1'b0),.media_slot(8'd0),.media_kind(8'h31),.media_read_only(1'b0),
            .media_image_size(32'd65536),.media_generation(32'd1),.media_complete_request_id(32'd0),.media_complete_status(32'd0),
            .media_complete_block_count(16'd0),.media_complete_generation(32'd0),
            .media_request_valid(),.media_request_write(),.media_request_id(),.media_request_lba(),.media_request_block_count(),.media_request_generation(),
            .media_write_data(),.media_write_data_valid(),.media_write_data_last(),.media_write_data_ready(1'b0),
            .media_state_mounted(),.media_state_dirty(),.media_state_eject_pending(),
            .core_id(),.ready(modeled_ready),.load_error(error),.cart_loaded(cart_loaded),.cart_size(cart_size),
            .video_r(),.video_g(),.video_b(),.video_de(),.video_hsync(),.video_vsync(),.video_valid(),.video_mode_240(),
            .audio_l(),.audio_r(),.audio_valid());
        assign config_valid=adapter_i.config_valid;
        assign cart_download=adapter_i.cart_download;
        assign cart_word_write=adapter_i.cart_word_write;
        assign cart_word_address=adapter_i.cart_word_address;
        assign cart_word_data=adapter_i.cart_word_data;
        assign save_address=adapter_i.loader_save_addr;
        assign save_data=adapter_i.loader_save_data;
        assign save_write=adapter_i.loader_save_write;
        assign write_addr=adapter_i.loader_addr;
        assign write_data=adapter_i.loader_data;
        assign write_strobe=adapter_i.loader_strobe;
        assign write_valid=adapter_i.loader_valid;
        assign write_ready=adapter_i.loader_ready;
        assign write_response=adapter_i.loader_response;
        assign stale_byte=adapter_i.loader_i.stale_byte;
        assign boot_signature=adapter_i.machine_i.ram68k_i.memory[15'h2008]===16'ha55a;
        always @* physical_wait=phy_wait;
        multitini_memory_cdc memory_i(
            .core_clk(core_clk),.core_resetn(core_resetn),.core_request_addr(memory_addr),.core_request_write(memory_write),
            .core_request_wdata(memory_wdata),.core_request_wstrb(memory_wstrb),.core_request_valid(memory_valid),.core_request_ready(memory_ready),
            .core_response_rdata(memory_rdata),.core_response_valid(memory_rvalid),
            .memory_clk(control_clk),.memory_resetn(control_resetn),.memory_request_addr(service_addr),.memory_request_write(service_write),
            .memory_request_wdata(service_wdata),.memory_request_wstrb(service_wstrb),.memory_request_valid(service_valid),.memory_request_ready(service_ready),
            .memory_response_rdata(service_rdata),.memory_response_valid(service_rvalid));
        multitini_psram_line_service service_i(
            .clk(control_clk),.resetn(control_resetn),.initialized(initialized),.fault(memory_fault),
            .request_addr(service_addr),.request_write(service_write),.request_wdata(service_wdata),.request_wstrb(service_wstrb),
            .request_valid(service_valid),.request_ready(service_ready),.response_rdata(service_rdata),.response_valid(service_rvalid),
            .command_valid(command_valid),.command_ready(command_ready),.command(command),.command_addr(command_addr),.command_wdata(command_wdata),
            .driver_response_valid(driver_rvalid),.driver_response_rdata(driver_rdata));
        assign command_ready=phy_wait==0 && !driver_rvalid;
        // The actual line service maps each logical 8-byte line onto four
        // physical byte addresses per device. Device replies remain modeled.
        always @(posedge control_clk)begin
            driver_rvalid<=0;
            if(control_resetn && command_valid && command_ready)begin
                driver_command<=command;driver_address<=command_addr;driver_data<=command_wdata;
                if((command==8'h02 || command==8'heb) && (command_addr[1:0]!=0 || command_addr>=CART_BYTES/2))
                    $fatal(1,"PSRAM service escaped original cartridge %h",command_addr);
                phy_wait<=command==8'h02 && (command_addr==(CART_BYTES-8)/2 || stress_abort) ? 400 : 17;
            end else if(phy_wait>0)begin
                phy_wait<=phy_wait-1;
                if(phy_wait==1)begin
                    if(driver_command==8'h02)begin
                        device_lines[driver_address>>2]<=driver_data;
                        for(integer lane=0;lane<8;lane=lane+1)rom[integer'(driver_address)*2+lane]<=driver_data[lane*8+:8];
                    end
                    driver_rdata<=driver_command==8'heb ? device_lines[driver_address>>2] : driver_data;
                    driver_rvalid<=1;
                end
            end
        end
        always @(negedge reset_request)if(core_resetn && cart_loaded && config_valid)begin
            for(integer offset=0;offset<65536;offset=offset+1)
                if(adapter_i.machine_i.cartridge_i.save_memory[offset]!==save_byte(offset))
                    $fatal(1,"Actual cartridge SRAM restore mismatch at %0d",offset);
        end
        always @(posedge core_clk)if(core_ticks%500000==0 && core_resetn)
            $display("TRACE actual round=%0d loaded=%b ready=%b reset=%b chip_reset=%b run=%b addr=%h cached=%b pending=%b state=%0d signature=%h",
                round_id,cart_loaded,modeled_ready,reset_request,adapter_i.machine_i.chip_reset,adapter_i.run_chip,
                adapter_i.rom_addr,adapter_i.rom_cached,adapter_i.cache_i.pending,adapter_i.cache_i.state,
                adapter_i.machine_i.ram68k_i.memory[15'h2008]);
    end
`endif
    endgenerate

    function automatic [7:0] cart_byte(input integer offset);
        cart_byte=ACTUAL_ADAPTER ? program_rom[offset] : 8'((offset*37)^(offset>>8)^(round_id*91));
    endfunction
    function automatic [7:0] save_byte(input integer offset);
        save_byte=8'((offset*13)^(offset>>8)^(round_id*53));
    endfunction
    function automatic [7:0] config_byte(input integer offset);
        case(offset)
            0:config_byte="M";1:config_byte="D";2:config_byte="0";3:config_byte="1";
            4:config_byte=1;5:config_byte=3;7:config_byte=1;
            default:config_byte=0;
        endcase
    endfunction
    function automatic [31:0] packed_word(input [7:0] kind,input integer offset);
        for(integer lane=0;lane<4;lane=lane+1)
            packed_word[lane*8+:8]=kind==MT_LOAD_GENESIS_CONFIG ? config_byte(offset+lane) :
                kind==MT_LOAD_GENESIS_CART ? cart_byte(offset+lane) : save_byte(offset+lane);
    endfunction
    always @(posedge core_clk)begin
        core_ticks<=core_ticks+1;model_write_response<=0;
        if(write_valid && write_ready)begin
            if(!ACTUAL_ADAPTER && physical_wait!=0)$fatal(1,"Overlapping physical writes");
            if(write_addr[2:0]!=0 || write_addr>=CART_BYTES || write_strobe!=8'hff)$fatal(1,"Wrong ROM write framing");
            for(integer lane=0;lane<8;lane=lane+1)
                if(write_data[lane*8+:8]!==cart_byte(integer'(write_addr)+lane))$fatal(1,"Wrong packed ROM byte");
            if(!ACTUAL_ADAPTER)begin
                physical_addr<=write_addr;physical_data<=write_data;physical_strobe<=write_strobe;
                physical_wait<=write_addr==CART_BYTES-8 || stress_abort ? 400 : 11+(core_ticks%7);
            end
            cart_writes<=cart_writes+1;
        end else if(!ACTUAL_ADAPTER && physical_wait>0)begin
            physical_wait<=physical_wait-1;
            if(physical_wait==1)begin
                for(integer lane=0;lane<8;lane=lane+1)
                    if(physical_strobe[lane])rom[integer'(physical_addr)+lane]<=physical_data[lane*8+:8];
                model_write_response<=1;completions<=completions+1;
            end
        end
        if(dst_valid && dst_ready && !stale_byte && dst_first && dst_kind==MT_LOAD_GENESIS_CART)begin
            cart_writes<=0;cart_words<=0;
        end
        if(cart_word_write)begin
            if(integer'(cart_word_address)!=cart_words || cart_word_data!=={cart_byte(cart_words*2),cart_byte(cart_words*2+1)})
                $fatal(1,"Wrong mapper word order");
            cart_words<=cart_words+1;
        end
        if(dst_valid && dst_ready && !stale_byte && dst_first && dst_kind==MT_LOAD_GENESIS_SAVE)save_writes<=0;
        if(save_write)begin
            if(integer'(save_address)!=save_writes || save_data!==save_byte(save_writes))$fatal(1,"Wrong SAVE byte order at %0d",save_writes);
            save_memory[save_address]<=save_data;save_writes<=save_writes+1;
        end
        if(monitor_save_overlap && dst_valid && dst_kind==MT_LOAD_GENESIS_SAVE && dst_first && physical_wait>0)begin
            if(dst_ready || cart_loaded || error)$fatal(1,"SAVE crossed before final ROM completion");
            overlap_cycles<=overlap_cycles+1;
        end
        if(dst_valid && dst_ready && stale_byte)abort_drains<=abort_drains+1;
        if(error)$fatal(1,"Loader rejected real shell sequence kind%h offset%0d reset%0b epoch%0b",dst_kind,dst_offset,reset_request,dst_epoch);
    end
    task automatic write_reg(input [11:0] address,input [31:0] value);
        begin
            @(negedge control_clk);wr_addr=address;wr_data=value;wr_strb=4'hf;wr_en=1;
            @(negedge control_clk);wr_en=0;wr_strb=0;#0.001;
        end
    endtask
    integer command_sequence=0;
    task automatic command(input [31:0] opcode,arg0,arg1);
        begin
            command_sequence=command_sequence+1;
            write_reg(MT_REG_COMMAND_ARG0,arg0);write_reg(MT_REG_COMMAND_ARG1,arg1);
            write_reg(MT_REG_COMMAND_SEQ,command_sequence);write_reg(MT_REG_COMMAND_OPCODE,opcode);
            write_reg(MT_REG_COMMAND_DOORBELL,command_sequence);
            rd_addr=MT_REG_RESPONSE_STATUS;#0.001;
            if(rd_data!==MT_RESPONSE_OK)$fatal(1,"Command%0h response%0h",opcode,rd_data);
        end
    endtask
    task automatic wait_status(input [31:0] mask,input bit asserted);
        integer polls;
        begin
            rd_addr=MT_REG_STATUS;#0.001;polls=0;
            while(((rd_data & mask)!=0)!=asserted)begin
                @(negedge control_clk);polls=polls+1;
                if(polls>100000)$fatal(1,"Status timeout mask%h status%h",mask,rd_data);
            end
        end
    endtask
    task automatic wait_stream(input [31:0] mask);
        integer polls;
        begin
            rd_addr=MT_REG_STREAM_STATUS;#0.001;polls=0;
            while((rd_data & mask)==0)begin
                @(negedge control_clk);polls=polls+1;
                if(polls>100000)$fatal(1,"Stream timeout mask%h offset%0d",mask,regs_i.stream_offset);
            end
        end
    endtask
    task automatic send_stream(input [7:0] kind,input integer total);
        begin
            command(MT_COMMAND_STREAM_BEGIN,{24'd0,kind},total);
            for(integer offset=0;offset<total;offset=offset+4)begin
                wait_stream(MT_STREAM_STATUS_READY);
                write_reg(MT_REG_STREAM_DATA_BURST,packed_word(kind,offset));
            end
            wait_stream(MT_STREAM_STATUS_DRAINED);
            if(kind==MT_LOAD_GENESIS_CART && cart_loaded)
                $fatal(1,"Final ROM response did not overlap DRAINED");
            command(MT_COMMAND_STREAM_END,0,0);
        end
    endtask
    task automatic boot_round(input bit after_abort);
        integer previous_overlap;
        reg expected_epoch;
        begin
            previous_overlap=overlap_cycles;
            expected_epoch=after_abort ? ~regs_i.stream_epoch : regs_i.stream_epoch;
            command(MT_COMMAND_CORE_RESET,1,0);
            write_reg(MT_REG_ERROR,MT_ERROR_CORE_LOAD);
            send_stream(MT_LOAD_GENESIS_CONFIG,16);
            send_stream(MT_LOAD_GENESIS_CART,CART_BYTES);
            monitor_save_overlap=1;
            send_stream(MT_LOAD_GENESIS_SAVE,65536);
            monitor_save_overlap=0;
            repeat(3)@(negedge core_clk);
            if(cart_writes!=CART_BYTES/8 || cart_words!=CART_BYTES/2 || save_writes!=65536 || !cart_loaded || !config_valid || cart_size!=CART_BYTES)
                $fatal(1,"Boot counts/state mismatch writes%0d words%0d saves%0d",cart_writes,cart_words,save_writes);
            if(overlap_cycles==previous_overlap)$fatal(1,"First SAVE byte did not wait for final ROM response");
            if(regs_i.stream_epoch!==expected_epoch)$fatal(1,"Normal stream changed epoch");
            for(integer offset=0;offset<CART_BYTES;offset=offset+1)
                if(rom[offset]!==cart_byte(offset))$fatal(1,"Physical ROM differs at %0d",offset);
            for(integer offset=0;offset<65536;offset=offset+1)
                if(save_memory[offset]!==save_byte(offset))$fatal(1,"Restored save differs at %0d",offset);
            command(MT_COMMAND_CORE_RESET,0,0);wait_status(MT_STATUS_CORE_READY,1);
            if((rd_data & MT_STATUS_CORE_LOAD_ERROR)!=0)$fatal(1,"Ready reported with load error");
            if(ACTUAL_ADAPTER)begin
                $display("TRACE real adapter ready round=%0d at%0t; waiting for68000 signature",round_id,$time);
                wait(boot_signature);
                if(error)$fatal(1,"Real 68000 boot raised load error");
            end
            rounds=rounds+1;
        end
    endtask
    task automatic put_word(input integer offset,input [15:0] value);
        begin program_rom[offset]=value[15:8];program_rom[offset+1]=value[7:0];end
    endtask
    initial begin
        for(integer i=0;i<1024;i=i+1)program_rom[i]=8'hff;
        put_word(0,16'h00ff);put_word(2,16'hff00);put_word(4,0);put_word(6,16'h0200);
        put_word(256,16'h5345);put_word(258,16'h4741);
        put_word(512,16'h46fc);put_word(514,16'h2700);
        put_word(516,16'h33fc);put_word(518,16'ha55a);put_word(520,16'h00ff);put_word(522,16'h0010);put_word(524,16'h60fe);
        repeat(5)@(negedge control_clk);control_resetn=1;core_resetn=1;
        round_id=1;boot_round(0);
        // Replace a running cartridge with no artificial hold or epoch change.
        round_id=2;boot_round(0);
        command(MT_COMMAND_CORE_RESET,1,0);
        write_reg(MT_REG_ERROR,MT_ERROR_CORE_LOAD);
        round_id=3;send_stream(MT_LOAD_GENESIS_CONFIG,16);
        stress_abort=1;command(MT_COMMAND_STREAM_BEGIN,{24'd0,MT_LOAD_GENESIS_CART},CART_BYTES);
        wait_stream(MT_STREAM_STATUS_READY);write_reg(MT_REG_STREAM_DATA_BURST,packed_word(MT_LOAD_GENESIS_CART,0));
        wait_stream(MT_STREAM_STATUS_READY);write_reg(MT_REG_STREAM_DATA_BURST,packed_word(MT_LOAD_GENESIS_CART,4));
        wait_stream(MT_STREAM_STATUS_READY);write_reg(MT_REG_STREAM_DATA_BURST,packed_word(MT_LOAD_GENESIS_CART,8));
        wait(dst_valid && dst_offset==8 && !dst_ready);
        command(MT_COMMAND_STREAM_ABORT,0,0);
        // Begin immediately after the command reply; abort delivery and the
        // old queued payload are still independent asynchronous handshakes.
        stress_abort=0;
        boot_round(1);
        if(abort_drains==0)$fatal(1,"Abort recovery did not drain queued stale byte");
        if(regs_i.stream_epoch!==1'b1)$fatal(1,"Recovery did not advance epoch exactly once");
        $display("PASS Genesis shell download: adapter=%0d, %0d CONFIG/CART/SAVE rounds, unchanged normal epoch, %0d final-write SAVE wait cycles, %0d stale abort bytes drained",ACTUAL_ADAPTER,rounds,overlap_cycles,abort_drains);
        $finish;
    end
    initial begin #120000000;$fatal(1,"Genesis shell download integration timeout round%0d ready%b loaded%b reset%b error%b words%0d saves%0d signature%b",round_id,modeled_ready,cart_loaded,reset_request,error,cart_words,save_writes,boot_signature);end
endmodule

// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// Actual AXI adapter -> register bank -> asynchronous byte mailbox -> GBC
// loader. Only the external 64-bit memory endpoint and CPU AXI master are
// modeled. This tests digital transport, not the PSRAM electrical interface.
module tb_axi_gameboy_loader;
    import multitini_regs_pkg::*;
    import multitini_gameboy_defs_pkg::*;
    reg control_clk=0,core_clk=0,control_resetn=0,core_resetn=0;
    always #3.75 control_clk=~control_clk;
    always #14.9 core_clk=~core_clk;
    reg [11:0] awid=0,wid=0,arid=0;
    reg [31:0] awaddr=0,wdata=0,araddr=0;
    reg awvalid=0,wvalid=0,bready=0,arvalid=0,rready=0;
    wire awready,wready,bvalid,arready,rvalid,rlast;
    wire [11:0] bid,rid;
    wire [1:0] bresp,rresp;
    wire [31:0] rdata;
    wire reg_wr_en,reg_rd_en;
    wire [15:0] reg_wr_addr,reg_rd_addr;
    wire [31:0] reg_wr_data,reg_rd_data,bank_rd_data,shell_control;
    wire [3:0] reg_wr_strb;
    wire load_submit,load_submit_ready,load_first,load_last,load_epoch;
    wire [7:0] load_kind,load_data;
    wire [31:0] load_offset,load_total;
    wire [82:0] destination;
    wire destination_valid,loader_ready;
    reg receiver_gate=0;
    wire [7:0] destination_kind=destination[82:75];
    wire [7:0] destination_data=destination[74:67];
    wire [31:0] destination_offset=destination[66:35];
    wire [31:0] destination_total=destination[34:3];
    wire destination_first=destination[2],destination_last=destination[1];
    wire loader_valid=destination_valid && receiver_gate;
    wire loader_active,loader_error,cart_loaded,boot_loaded;
    wire [31:0] cart_size;
    wire core_reset_request;
    reg core_ready_launch=0,core_error_launch=0;
    wire core_ready_control,core_error_control;
    // Same execution-reset terms as the adapter, with cart_ready=1 and no
    // physical-memory fault: the real CPU/mapper are outside this small test.
    wire execution_reset=!core_resetn || core_reset_request || loader_valid ||
                         loader_active || !cart_loaded;
    wire ioctl_download,cart_download,cgb_boot_download,ioctl_wr;
    wire [24:0] ioctl_addr;
    wire [15:0] ioctl_dout;
    wire [23:0] memory_addr;
    wire memory_write,memory_valid;
    wire [63:0] memory_wdata;
    wire [7:0] memory_wstrb;
    reg memory_ready=0,memory_rvalid=0;
    reg [63:0] memory_rdata=0;
    reg memory_pending=0,hold_final=0;
    reg [23:0] pending_addr;
    reg [63:0] pending_data;
    reg [7:0] pending_strobes;
    integer memory_delay=0;
    reg [7:0] stored[0:32783];
    integer job=0,total=0,accepted_bytes=0,committed_bytes=0,accepted_lines=0;
    integer checked_bytes=0,checked_header_bytes=0,metadata_words=0;
    integer read_count=0,aw_first=0,w_first=0,simultaneous=0;
    integer sequence_number=0,seed=1,discard_random;
    reg [15:0] expected_read_addr=0;
    reg stream_check=0;

    multitini_axi3_regs axi_i(
        .aclk(control_clk),.aresetn(control_resetn),
        .s_axi_awid(awid),.s_axi_awaddr(awaddr),.s_axi_awlen(4'd0),.s_axi_awsize(3'd2),
        .s_axi_awburst(2'd1),.s_axi_awlock(2'd0),.s_axi_awcache(4'd0),.s_axi_awprot(3'd0),.s_axi_awqos(4'd0),
        .s_axi_awvalid(awvalid),.s_axi_awready(awready),.s_axi_wid(wid),.s_axi_wdata(wdata),
        .s_axi_wstrb(4'hf),.s_axi_wlast(1'b1),.s_axi_wvalid(wvalid),.s_axi_wready(wready),
        .s_axi_bid(bid),.s_axi_bresp(bresp),.s_axi_bvalid(bvalid),.s_axi_bready(bready),
        .s_axi_arid(arid),.s_axi_araddr(araddr),.s_axi_arlen(4'd0),.s_axi_arsize(3'd2),
        .s_axi_arburst(2'd1),.s_axi_arlock(2'd0),.s_axi_arcache(4'd0),.s_axi_arprot(3'd0),.s_axi_arqos(4'd0),
        .s_axi_arvalid(arvalid),.s_axi_arready(arready),.s_axi_rid(rid),.s_axi_rdata(rdata),
        .s_axi_rresp(rresp),.s_axi_rlast(rlast),.s_axi_rvalid(rvalid),.s_axi_rready(rready),
        .reg_wr_en(reg_wr_en),.reg_wr_addr(reg_wr_addr),.reg_wr_data(reg_wr_data),.reg_wr_strb(reg_wr_strb),
        .reg_rd_en(reg_rd_en),.reg_rd_addr(reg_rd_addr),.reg_rd_data(reg_rd_data));
    assign reg_rd_data=reg_rd_addr<16'h1000 ? bank_rd_data : 32'd0;
    multitini_control_regs bank_i(
        .clk(control_clk),.resetn(control_resetn),.core_id(multitini_gameboy_defs_pkg::MT_GBC_CORE_ID),.core_variant_index(32'd0),
        .core_ready(core_ready_control),.load_submit_ready(load_submit_ready),.core_load_error(core_error_control),
        .vectrex_overlay_status(2'd0),.input_submit_ready(1'b1),
        .media_request_valid(1'b0),.media_request_write(1'b0),.media_request_id(32'd0),
        .media_request_lba(32'd0),.media_request_block_count(16'd0),.media_request_generation(32'd0),
        .media_data_valid(1'b0),.media_data(8'd0),.media_data_last(1'b0),
        .media_state_mounted(1'b0),.media_state_dirty(1'b0),.media_state_eject_pending(1'b0),.media_submit_ready(1'b1),
        .ui_active_visible(1'b0),.ui_active_bank(1'b0),.ui_active_dense(1'b0),.ui_submit_ready(1'b1),
        .effect_busy(1'b0),.effect_active_valid(1'b0),.effect_rejected(1'b0),.effect_active_sequence(32'd0),
        .wr_en(reg_wr_en && reg_wr_addr<16'h1000),.wr_addr(reg_wr_addr[11:0]),.wr_data(reg_wr_data),.wr_strb(reg_wr_strb),
        .rd_en(reg_rd_en && reg_rd_addr<16'h1000),.rd_addr(reg_rd_addr[11:0]),.rd_data(bank_rd_data),
        .control(shell_control),.load_submit(load_submit),.load_kind(load_kind),.load_data(load_data),
        .load_offset(load_offset),.load_total(load_total),.load_first(load_first),.load_last(load_last),.load_epoch(load_epoch));
    multitini_async_mailbox #(.WIDTH(83)) mailbox_i(
        .src_clk(control_clk),.src_resetn(control_resetn),
        .src_data({load_kind,load_data,load_offset,load_total,load_first,load_last,load_epoch}),
        .src_valid(load_submit),.src_ready(load_submit_ready),
        .dst_clk(core_clk),.dst_resetn(core_resetn),.dst_data(destination),.dst_valid(destination_valid),
        .dst_ready(loader_ready && receiver_gate));
    cdc_bit_sync reset_request_i(.clk(core_clk),.resetn(core_resetn),
        .din(|(shell_control & multitini_regs_pkg::MT_CONTROL_CORE_RESET_REQUEST)),.dout(core_reset_request));
    // Match the board's registered status launches and two-flop returns.
    always @(posedge core_clk)begin
        core_ready_launch<=core_resetn && !execution_reset;
        core_error_launch<=core_resetn && loader_error;
    end
    cdc_bit_sync core_ready_i(.clk(control_clk),.resetn(control_resetn),
        .din(core_ready_launch),.dout(core_ready_control));
    cdc_bit_sync core_error_i(.clk(control_clk),.resetn(control_resetn),
        .din(core_error_launch),.dout(core_error_control));
    multitini_gameboy_loader loader_i(
        .clk(core_clk),.reset(!core_resetn),.clear_error(1'b0),
        .load_data(destination_data),.load_valid(loader_valid),.load_ready(loader_ready),
        .load_first(destination_first),.load_last(destination_last),.load_kind(destination_kind),
        .load_offset(destination_offset),.load_total(destination_total),
        .ioctl_download(ioctl_download),.cart_download(cart_download),.cgb_boot_download(cgb_boot_download),
        .ioctl_wr(ioctl_wr),.ioctl_addr(ioctl_addr),.ioctl_dout(ioctl_dout),
        .memory_addr(memory_addr),.memory_write(memory_write),.memory_wdata(memory_wdata),.memory_wstrb(memory_wstrb),
        .memory_valid(memory_valid),.memory_ready(memory_ready),.memory_rdata(memory_rdata),.memory_rvalid(memory_rvalid),
        .cart_loaded(cart_loaded),.boot_override_loaded(boot_loaded),.cart_size(cart_size),.active(loader_active),.error(loader_error));

    task automatic check(input logic condition,input string message);
        if(condition!==1'b1)begin
            $display("FAIL: job=%0d accepted=%0d committed=%0d %s",job,accepted_bytes,committed_bytes,message);
            $fatal(1);
        end
    endtask
    function automatic [7:0] pattern(input integer index,input integer which);
        pattern=((index*37)^(index>>3)^(index>>8)^(which*91)^8'ha6);
    endfunction
    always @(posedge control_clk)if(control_resetn && reg_rd_en)begin
        check(reg_rd_addr==expected_read_addr,"read side effect retains accepted ARADDR");
        read_count=read_count+1;
    end
    always @(negedge core_clk)begin
        receiver_gate=core_resetn && $urandom_range(0,3)!=0;
        memory_ready=core_resetn && !memory_pending && $urandom_range(0,3)!=0;
    end
    reg memory_was_stalled=0;
    reg [96:0] held_memory;
    always @(posedge core_clk)begin : memory_endpoint
        integer lane;
        integer expected_strobes;
        memory_rvalid<=0;
        if(!core_resetn)begin
            memory_pending<=0;memory_was_stalled<=0;
        end else begin
            if(memory_was_stalled)
                check(memory_valid && {memory_addr,memory_write,memory_wdata,memory_wstrb}===held_memory,
                      "loader memory request stable under backpressure");
            memory_was_stalled<=memory_valid && !memory_ready;
            held_memory<={memory_addr,memory_write,memory_wdata,memory_wstrb};
            if(loader_valid && loader_ready)begin
                check(stream_check && destination_kind==MT_LOAD_GB_CART,"only active cartridge stream accepted");
                check(destination_offset==accepted_bytes && destination_total==total,"byte framing/order");
                check(destination_data==pattern(accepted_bytes,job),"every mailbox byte matches independent pattern");
                check(destination_first==(accepted_bytes==0) && destination_last==(accepted_bytes==total-1),"first/last framing");
                accepted_bytes=accepted_bytes+1;
            end
            if(ioctl_wr)begin
                check(ioctl_download && cart_download,"metadata qualifier covers registered write");
                check(ioctl_addr==metadata_words*2,"metadata words stay consecutive");
                check(ioctl_dout=={pattern(metadata_words*2+1,job),pattern(metadata_words*2,job)},"metadata/header word bytes");
                metadata_words=metadata_words+1;
            end
            if(memory_valid && memory_ready)begin
                check(!memory_pending && memory_write,"single outstanding write");
                check(memory_addr==accepted_lines*8,"every line address consecutive");
                expected_strobes=(total-memory_addr>=8) ? 255 : ((1<<(total-memory_addr))-1);
                check(memory_wstrb==expected_strobes,"final line byte enables");
                pending_addr<=memory_addr;pending_data<=memory_wdata;pending_strobes<=memory_wstrb;
                memory_delay<=$urandom_range(2,23);memory_pending<=1;
                accepted_lines=accepted_lines+1;
            end
            if(memory_pending)begin
                if(memory_delay!=0)memory_delay<=memory_delay-1;
                else if(!(hold_final && pending_addr+8>=total))begin
                    for(lane=0;lane<8;lane=lane+1)if(pending_strobes[lane])begin
                        check(pending_addr+lane<total,"no write beyond cartridge size");
                        check(pending_data[lane*8+:8]==pattern(pending_addr+lane,job),"every stored cartridge byte");
                        stored[pending_addr+lane]=pending_data[lane*8+:8];
                        committed_bytes=committed_bytes+1;
                    end
                    memory_rdata<=pending_data;memory_rvalid<=1;memory_pending<=0;
                end
            end
            if(stream_check && (loader_active || committed_bytes<total))
                check(execution_reset,"execution held while cartridge load or final write is incomplete");
            check(!loader_error,"loader framing remains valid");
        end
    end

    task automatic write_reg(input [15:0] address,input [31:0] value);
        integer ad,wd;reg [11:0] id;
        begin
            ad=$urandom_range(0,6);wd=$urandom_range(0,6);id=$urandom;
            if(ad<wd)aw_first=aw_first+1;else if(wd<ad)w_first=w_first+1;else simultaneous=simultaneous+1;
            fork
                begin
                    repeat(ad+1)@(negedge control_clk);
                    awid=id;awaddr={16'h4000,address};awvalid=1;
                    do @(posedge control_clk);while(!awready);
                    @(negedge control_clk);awvalid=0;awaddr=32'hdeadbeef;
                end
                begin
                    repeat(wd+1)@(negedge control_clk);
                    wid=id;wdata=value;wvalid=1;
                    do @(posedge control_clk);while(!wready);
                    @(negedge control_clk);wvalid=0;wdata=32'hbad0bad0;
                end
            join
            wait(bvalid);#1;check(bresp==0 && bid==id,"AXI write response");
            repeat($urandom_range(0,5)+1)@(negedge control_clk);
            bready=1;@(posedge control_clk);@(negedge control_clk);bready=0;
        end
    endtask
    task automatic read_reg(input [15:0] address,output [31:0] value);
        reg [11:0] id;integer n,initial_count;
        begin
            id=$urandom;initial_count=read_count;
            repeat($urandom_range(0,3)+1)@(negedge control_clk);
            expected_read_addr=address;arid=id;araddr={16'h4000,address};arvalid=1;
            do @(posedge control_clk);while(!arready);
            @(negedge control_clk);arvalid=0;araddr=32'h40000a44;arid=~id;
            wait(rvalid);#1;value=rdata;
            check(rresp==0 && rid==id && rlast,"AXI read response");
            for(n=0;n<3;n=n+1)begin
                @(negedge control_clk);araddr=$urandom;
                check(rvalid && rdata===value && rid==id && !arready,"held read response stable");
            end
            rready=1;@(posedge control_clk);@(negedge control_clk);rready=0;
            check(read_count==initial_count+1,"one bank read per accepted AR");
        end
    endtask
    task automatic command(input [31:0] opcode,input [31:0] arg0,input [31:0] arg1);
        reg [31:0] value;
        begin
            sequence_number=sequence_number+1;
            write_reg(MT_REG_COMMAND_ARG0,arg0);write_reg(MT_REG_COMMAND_ARG1,arg1);
            write_reg(MT_REG_COMMAND_SEQ,sequence_number);write_reg(MT_REG_COMMAND_OPCODE,opcode);
            write_reg(MT_REG_COMMAND_DOORBELL,sequence_number);
            read_reg(MT_REG_RESPONSE_SEQ,value);check(value==sequence_number,"command completion sequence");
            read_reg(MT_REG_RESPONSE_STATUS,value);check(value==MT_RESPONSE_OK,"command accepted");
        end
    endtask
    task automatic upload(input integer length,input integer which,input bit delayed_final);
        integer offset,lane,polls;reg [31:0] value,word_value;
        begin
            command(MT_COMMAND_CORE_RESET,1,0);
            wait(core_reset_request);
            total=length;job=which;accepted_bytes=0;committed_bytes=0;accepted_lines=0;metadata_words=0;
            hold_final=delayed_final;stream_check=1;
            for(offset=0;offset<32784;offset=offset+1)stored[offset]=8'h5c;
            command(MT_COMMAND_STREAM_BEGIN,MT_LOAD_GB_CART,total);
            for(offset=0;offset<total;offset=offset+4)begin
                polls=0;
                read_reg(MT_REG_STREAM_STATUS,value);
                while(!(value & MT_STREAM_STATUS_READY))begin
                    polls=polls+1;check(polls<2000,"stream READY timeout");
                    read_reg(MT_REG_STREAM_STATUS,value);
                end
                if((offset & 255)==0)begin
                    write_reg(MT_REG_SCRATCH,32'hac000000|offset);
                    read_reg(MT_REG_SCRATCH,value);check(value==(32'hac000000|offset),"unrelated read/write interleaving");
                end
                word_value=32'hffffffff;
                for(lane=0;lane<4;lane=lane+1)if(offset+lane<total)
                    word_value[lane*8+:8]=pattern(offset+lane,job);
                write_reg(MT_REG_STREAM_DATA_BURST,word_value);
            end
            polls=0;read_reg(MT_REG_STREAM_STATUS,value);
            while(!(value & MT_STREAM_STATUS_DRAINED))begin
                polls=polls+1;check(polls<2000,"stream DRAINED timeout");
                read_reg(MT_REG_STREAM_STATUS,value);
            end
            check(accepted_bytes==total,"drain means every byte reached destination");
            command(MT_COMMAND_STREAM_END,0,0);
            command(MT_COMMAND_CORE_RESET,0,0);
            if(delayed_final)begin
                repeat(10)@(negedge core_clk);
                check(loader_active && !cart_loaded && execution_reset && committed_bytes<total,
                      "drained mailbox cannot release execution before final memory completion");
                read_reg(MT_REG_STATUS,value);
                check(!(value & MT_STATUS_CORE_READY),"firmware status remains unready before final memory completion");
                hold_final=0;
            end
            wait(cart_loaded && !loader_active && !execution_reset);
            @(negedge core_clk);
            check(cart_size==total && committed_bytes==total,"all bytes committed before loaded");
            check(metadata_words==total/2,"all complete metadata words emitted");
            for(offset=0;offset<total;offset=offset+1)begin
                check(stored[offset]==pattern(offset,job),"complete cartridge scoreboard");
                checked_bytes=checked_bytes+1;
                if(offset>=32'h104 && offset<=32'h14f)checked_header_bytes=checked_header_bytes+1;
            end
            check(stored[total]==8'h5c,"tail strobe preserves next byte");
            polls=0;read_reg(MT_REG_STATUS,value);
            while(!(value & MT_STATUS_CORE_READY))begin
                polls=polls+1;check(polls<100,"core READY status timeout after complete upload");
                read_reg(MT_REG_STATUS,value);
            end
            read_reg(MT_REG_ERROR,value);check(value==0,"bank has no rejected transfers");
            stream_check=0;
            wait(load_submit_ready && !destination_valid && !memory_pending);
            repeat(10)@(negedge core_clk);
            $display("PASS job=%0d bytes=%0d lines=%0d metadata_words=%0d",job,total,accepted_lines,metadata_words);
        end
    endtask

    initial begin
        reg [31:0] value;
        if($value$plusargs("SEED=%d",seed))begin end
        discard_random=$urandom(seed);
        repeat(8)@(negedge control_clk);control_resetn=1;
        repeat(6)@(negedge core_clk);core_resetn=1;
        read_reg(MT_REG_MAGIC,value);check(value==MT_MAGIC,"shell constant through actual bank");
        upload(32768,1,1);
        upload(32771,2,0); // Legal loader framing; deliberately synthetic odd size exercises both tails.
        // Cancel a pending AXI read only after CDC/memory have fully drained.
        @(negedge control_clk);expected_read_addr=MT_REG_SCRATCH;
        araddr=32'h40000000 | MT_REG_SCRATCH;arvalid=1;
        do @(posedge control_clk);while(!arready);
        @(negedge control_clk);control_resetn=0;core_resetn=0;arvalid=0;
        repeat(8)@(negedge control_clk);check(!rvalid,"reset cancels pending read");control_resetn=1;
        repeat(6)@(negedge core_clk);core_resetn=1;
        read_reg(MT_REG_SCRATCH,value);check(value==0,"actual bank reset after drained stream");
        upload(32769,3,1);
        check(aw_first>0 && w_first>0 && simultaneous>0,"all independent AXI write channel orders covered");
        $display("PASS: AXI to GBC loader integration bytes=%0d header_bytes=%0d reads=%0d aw_first=%0d w_first=%0d simultaneous=%0d",
                 checked_bytes,checked_header_bytes,read_count,aw_first,w_first,simultaneous);
        $finish;
    end
    initial begin #200000000; $fatal(1,"integration watchdog timeout");end
endmodule

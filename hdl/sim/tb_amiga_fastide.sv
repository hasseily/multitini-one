// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Original diagnostic program executes on the real stock/accelerated CPUs.
// Only the reset timer is shortened. The native bus, clock gate and coherent
// production memory adapter are unmodified. The DDR responder adds variable
// latency so the CPU has to survive real timeline holds.
module tb_amiga_fastide #(parameter integer CPU_MODE=3, parameter bit CLASSIC=0);
    reg clk=0;always #17.62 clk=~clk;
    reg reset=1,guest_reset=1;
    wire chip_clk,chip_tick,c1,c3,en7,en7n,run_enable;
    wire memory_run;reg injected_hold=0;assign run_enable=memory_run && !injected_hold;
    wire [23:1] chip_addr;
    wire [15:0] chip_wdata,chip_wdata2,chip_rdata,chip_rdata2;
    wire [47:0] chip_rdata48;
    wire chip_oe_n,chip_we_n,chip_ub_n,chip_lb_n,chip_long;
    wire cpu_req,cpu_ub_n,cpu_lb_n,cpu_long,cpu_ready,cpu_reset_n;
    wire [28:1] cpu_addr;
    wire [15:0] cpu_wdata,cpu_wdata2,cpu_rdata,cpu_rdata2;
    wire [1:0] cpu_state;
    reg io_uio=0,io_strobe=0;reg [15:0] io_din=0;
    wire [4:0] cpucfg;
    wire [23:0] memory_addr;
    wire memory_write,memory_valid,memory_busy,memory_fault;
    wire [63:0] memory_wdata;
    wire [7:0] memory_wstrb;
    reg [63:0] response_data=0;
    reg response_valid=0,response_busy=0;
    reg [7:0] response_delay=0;
    reg [23:0] held_addr;
    reg held_write;
    reg [63:0] held_data;
    reg [7:0] held_mask;
    reg [63:0] chip_store=0,fast_store=0,blit_store=0,repeat_store=0;
    reg [7:0] ram[0:16383];reg [15:0] rom[0:4095];
    wire [4:0] ide_address;wire ide_read,ide_write,actual_fast;wire [15:0] ide_wdata,ide_rdata;wire [5:0] ide_req;
    reg control_clk=0,control_resetn=0,write=0,block_write=0,block_read=0;always #3.75 control_clk=~control_clk;
    reg [31:0] command=0;wire [31:0] status,block_rdata;
    reg [15:0] block_address=0,block_read_address=0;reg [6:0] block_ram_read_address=0;reg [3:0] block_wstrb=0;
    wire [6:0] memcfg;
    integer transactions=0,holds=0,cycles;
    integer completions=0,dma_source_hits=0,dma_source_misses=0;
    integer speed_edges=0,speed_opportunities=0,kernel_steps=0;
    reg previous_speed_ce=0,speed_phase_valid=0;
    always @(posedge chip_clk)begin
        if(!cpu_reset_n)begin
            speed_edges=0;speed_opportunities=0;kernel_steps=0;
            speed_phase_valid=0;previous_speed_ce=0;
        end else if(CPU_MODE!=0)begin
            speed_edges=speed_edges+1;
            if(dut.cpu.cpu_speed_ce)speed_opportunities=speed_opportunities+1;
            if(dut.cpu_step)kernel_steps=kernel_steps+1;
            if(CPU_MODE==7 && speed_phase_valid && dut.cpu.cpu_speed_ce==previous_speed_ce)
                $fatal(1,"14 MHz execution opportunity did not alternate native edges");
            if(CPU_MODE==3 && !dut.cpu.cpu_speed_ce)
                $fatal(1,"28 MHz execution opportunity was throttled");
            if(CPU_MODE==7 && dut.cpu_step && !dut.cpu.cpu_speed_ce)
                $fatal(1,"14 MHz kernel consumed a disabled edge");
            previous_speed_ce=dut.cpu.cpu_speed_ce;speed_phase_valid=1;
        end
    end
    // Both copies use the same source: its first lookup misses after the
    // Fast RAM alias evicts it, while the repeated DMA lookup must hit.
    always @(posedge clk)if(!reset && memory_i.state==0 && memory_i.request_valid &&
        memory_i.request_owner==0 && !memory_i.request_write && memory_i.request_addr==24'h001000)begin
        if(memory_i.hit)dma_source_hits=dma_source_hits+1;
        else dma_source_misses=dma_source_misses+1;
    end
    always @(posedge chip_clk)if(cpu_req && cpu_ready && completions<48)begin
        completions=completions+1;
        $display("CPU completion addr=%h state=%b rdata=%h din=%h udslds=%b%b",{cpu_addr,1'b0},cpu_state,cpu_rdata,dut.cpu.cpu_din,cpu_ub_n,cpu_lb_n);
    end
    always @(posedge chip_clk)if(CPU_MODE==3 && dut.cpu.clkena_p_throttled && dut.cpu.cpu_req && completions<48)begin
        completions=completions+1;
        $display("TG consume addr=%h state=%b din=%h chipaddr=%h chipdata=%h ready=%b",dut.cpu.cpu_addr,cpu_state,dut.cpu.cpu_din,{chip_addr,1'b0},chip_rdata,dut.cpu.chipready);
    end

    multitini_amiga_machine dut(
        .clk_service28(clk),.reset(reset),.guest_reset(guest_reset),.run_enable(run_enable),
        .clk_chip(chip_clk),.chip_tick(chip_tick),.chip_c1(c1),.chip_c3(c3),.chip_clk7_en(en7),.chip_clk7n_en(en7n),
        .chip_addr(chip_addr),.chip_wdata(chip_wdata),.chip_wdata2(chip_wdata2),
        .chip_oe_n(chip_oe_n),.chip_we_n(chip_we_n),.chip_ub_n(chip_ub_n),.chip_lb_n(chip_lb_n),.chip_long(chip_long),
        .chip_rdata(chip_rdata),.chip_rdata2(chip_rdata2),.chip_rdata48(chip_rdata48),
        .cpu_req(cpu_req),.cpu_addr(cpu_addr),.cpu_wdata(cpu_wdata),.cpu_wdata2(cpu_wdata2),
        .cpu_state(cpu_state),.cpu_ub_n(cpu_ub_n),.cpu_lb_n(cpu_lb_n),.cpu_long(cpu_long),
        .cpu_rdata(cpu_rdata),.cpu_rdata2(cpu_rdata2),.cpu_ready(cpu_ready),.cpu_ready_long(cpu_ready),
        .io_uio(io_uio),.io_fpga(1'b0),.io_strobe(io_strobe),.io_din(io_din),.io_wait(),.io_dout(),
        .ide_address(ide_address),.ide_read(ide_read),.ide_write(ide_write),.ide_wdata(ide_wdata),.ide_rdata(ide_rdata),.ide_req(ide_req),.ide_fast(actual_fast),.memcfg(memcfg),
        .joy1(16'hffff),.joy2(16'hffff),.mouse_buttons(3'd0),.kms_level(1'b0),
        .kbd_mouse_type(2'd0),.kbd_mouse_data(8'd0),.rtc(65'd0),.cpucfg(cpucfg),.cpu_reset_n(cpu_reset_n));
    multitini_amiga_memory memory_i(
        .clk(clk),.reset(reset),.chip_c1(c1 && !guest_reset),.chip_addr(chip_addr),
        .chip_wdata(chip_wdata),.chip_wdata2(chip_wdata2),.chip_oe_n(chip_oe_n),.chip_we_n(chip_we_n),
        .chip_ub_n(chip_ub_n),.chip_lb_n(chip_lb_n),.chip_long(chip_long),
        .chip_rdata(chip_rdata),.chip_rdata2(chip_rdata2),.chip_rdata48(chip_rdata48),
        .cpu_req(cpu_req && cpu_reset_n && !guest_reset),.cpu_addr(cpu_addr),.cpu_wdata(cpu_wdata),.cpu_wdata2(cpu_wdata2),
        .cpu_state(cpu_state),.cpu_ub_n(cpu_ub_n),.cpu_lb_n(cpu_lb_n),.cpu_long(cpu_long),
        .cpu_rdata(cpu_rdata),.cpu_rdata2(cpu_rdata2),.cpu_ready(cpu_ready),
        .load_valid(1'b0),.load_addr(24'd0),.load_data(8'd0),.load_ready(),.load_done(),
        .run_enable(memory_run),.busy(memory_busy),.fault(memory_fault),
        .memory_addr(memory_addr),.memory_write(memory_write),.memory_wdata(memory_wdata),.memory_wstrb(memory_wstrb),
        .memory_valid(memory_valid),.memory_ready(!response_busy),.memory_rdata(response_data),
        .memory_rvalid(response_valid),.memory_fault(1'b0));

    multitini_amiga_host host_i(
        .control_clk(control_clk),.control_resetn(control_resetn),.write(write),.command(command),.status(status),
        .block_write(block_write),.block_read(block_read),.block_address(block_address),
        .block_read_address(block_read_address),.block_ram_read_address(block_ram_read_address),
        .block_wstrb(block_wstrb),.block_rdata(block_rdata),
        .clk(clk),.reset(reset),.guest_reset(guest_reset),.native_config({3'd0,actual_fast,memcfg,cpucfg}),.chip_tick(chip_tick),
        .io_fpga(),.io_uio(),.io_strobe(),.io_din(),.io_wait(1'b0),.io_dout(16'd0),
        .ide_req(ide_req),.ide_address(ide_address),.ide_read(ide_read),.ide_write(ide_write),.ide_wdata(ide_wdata),.ide_rdata(ide_rdata));
    function automatic [63:0] read_line(input [23:0] address);
        integer n;reg [15:0] word_value;
        begin
            read_line=0;
            if(address<16384)for(n=0;n<8;n=n+1)read_line[n*8+:8]=ram[address+n];
            else if(address>=24'h780000 && address<24'h782000)for(n=0;n<4;n=n+1)begin
                word_value=rom[(address-24'h780000)/2+n];
                read_line[16*n+:16]={word_value[7:0],word_value[15:8]};
            end
        end
    endfunction
    integer lane,service_cycle=0,fast_reads=0,classic_reads=0,irq_edges=0,held_reads=0;
    always @(negedge clk)begin
        service_cycle=service_cycle+1;
        injected_hold=(service_cycle%127>=113);
    end
    always @(posedge chip_clk)if(cpu_reset_n)begin
        if(dut.fastchip_ready && dut.fastchip_rnw)begin
            fast_reads=fast_reads+1;
            if(CPU_MODE==7 && !dut.cpu.cpu_speed_ce)held_reads=held_reads+1;
        end
        if(!dut.cpu_chip_as && dut.cpu_chip_addr[23:16]==8'hda)begin
            classic_reads=classic_reads+1;
            if(actual_fast)$fatal(1,"Fast IDE transaction also entered legacy chip bus");
        end
        if((actual_fast ? dut.fast_ide_irq : dut.chipset.gayle_irq))begin
            irq_edges=irq_edges+1;
            if(dut.chipset.PAULA1.int2!==1'b1)$fatal(1,"Selected IDE IRQ failed to reach chipset INT2");
        end
    end
    always @(posedge clk)begin
        response_valid<=0;
        if(!reset && !run_enable)holds<=holds+1;
        if(response_busy)begin
            if(response_delay!=0)response_delay<=response_delay-1'b1;
            else begin
                if(held_write && held_addr<16384)for(lane=0;lane<8;lane=lane+1)
                    if(held_mask[lane])ram[held_addr+lane]<=held_data[lane*8+:8];
                response_data<=read_line(held_addr);response_valid<=1;response_busy<=0;
            end
        end else if(memory_valid)begin
            held_addr<=memory_addr;held_write<=memory_write;held_data<=memory_wdata;held_mask<=memory_wstrb;
            response_delay<=4+(transactions%13);response_busy<=1;transactions<=transactions+1;
        end
    end
    task automatic config_word(input [7:0] regno,input [15:0] value);
        @(negedge chip_clk);io_uio=1;io_din={8'd0,regno};io_strobe=1;
        @(negedge chip_clk);io_strobe=0;
        @(negedge chip_clk);io_din=value;io_strobe=1;
        @(negedge chip_clk);io_strobe=0;
        repeat(8)@(negedge chip_clk);io_uio=0;
        repeat(8)@(negedge chip_clk);
    endtask
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
    task automatic wait_request(input [2:0] value);
        integer n;
        begin
            n=0;while(status[18:16]!==value && n<200000)begin tick;n=n+1;end
            if(n==200000)$fatal(1,"IDE request timeout expected%0d actual%0d cpuPC%h marker%h",value,status[18:16],dut.cpu.cpu_addr,{ram[4096],ram[4097]});
        end
    endtask
    function automatic [15:0] ram_word(input integer address);ram_word={ram[address],ram[address+1]};endfunction
    integer n,partial_words;reg [31:0] value;reg [15:0] want0,want1;
    reg [7:0] expected[0:511];reg [31:0] expected_sum[0:0];
    initial begin
        for(n=0;n<16384;n=n+1)ram[n]=0;
        for(n=0;n<4096;n=n+1)rom[n]=16'h4e71;
        $readmemh("fastide-rom.hex",rom);$readmemh("fastide-expected.hex",expected);
        $readmemh("fastide-checksum.hex",expected_sum);
        repeat(12)tick;@(negedge clk);reset=0;control_resetn=1;
        repeat(32)@(negedge chip_clk);
        config_word(8'hf8,CLASSIC ? 16'h0027:16'h0007);
        config_word(8'hf4,CPU_MODE==0 ? 16'h0000:CPU_MODE==7 ? 16'h005f:16'h001f);
        // Uninitialized simulation FFs are not the FPGA's configuration INIT.
        // Hold the CPU reset while producing a clean chipset reset edge for
        // both native IDE channels. Only the frame-count delay is shortened.
        config_word(8'hf1,16'h0002);
        force dut.chipset.CONTROL1.rst_cnt=3'd4;
        repeat(32)@(negedge chip_clk);
        force dut.chipset.CONTROL1.rst_cnt=3'd0;
        repeat(32)@(negedge chip_clk);
        config_word(8'hf1,16'h0000);
        if(actual_fast!==(CPU_MODE!=0 && !CLASSIC))$fatal(1,"IDE backend configuration mismatch");
        legacy(11,0,0,0);
        if(status[15:0]!=={3'd0,actual_fast,memcfg,cpucfg} || !status[23])$fatal(1,"Fast IDE capability/actual configuration readback wrong");
        legacy(3,6,16'h0399,0);
        if(actual_fast)begin
            if(dut.fastchip_i.gayle.ide0.present!==3 || dut.chipset.GAYLE1.ide0.present!==0)$fatal(1,"Management presence leaked into inactive Classic controller");
        end else if(dut.chipset.GAYLE1.ide0.present!==3 || dut.fastchip_i.gayle.ide0.present!==0)$fatal(1,"Management presence leaked into inactive Fast controller");
        force dut.chipset.CONTROL1.rst_cnt=3'd4;
        guest_reset=0;
        wait_request(4);legacy(2,5,0,0);
        release dut.chipset.CONTROL1.rst_cnt;
        if(status[15:8]!==8'h20)$fatal(1,"Guest READ command not received:%h",status);
        legacy(3,0,1,0);fill(1);start_block(1,0);finish_block(1,0,256);
        legacy(3,1,0,0);legacy(3,2,0,0);legacy(3,5,16'h5ee0,0);
        wait_request(4);legacy(2,5,0,0);
        if(status[15:8]!==8'h30)$fatal(1,"Guest WRITE command not received:%h",status);
        if(ram_word(4096)!==2)$fatal(1,"Guest did not finish sector READ");
        for(n=0;n<512;n=n+1)if(n!=2 && n!=3 && ram[8192+n]!==expected[n])
            $fatal(1,"Guest READ buffer byte%0d expected%h got%h",n,expected[n],ram[8192+n]);
        if(ram[4352]!==0 || ram[4353]!==expected[2])$fatal(1,"Guest byte lanes/read pointer mismatch %h/%h",ram[4352],ram[4353]);
        if({ram_word(4112),ram_word(4114)}!==expected_sum[0])$fatal(1,"Actual CPU sector checksum mismatch");
        legacy(3,0,1,0);legacy(3,5,16'h58e0,0);wait_request(5);
        start_block(2,1);finish_block(2,0,256);
        for(n=0;n<128;n=n+1)begin
            cr(16'hd200+n*4,value);
            want0=16'h3456+(2*n)*16'h0113;want1=16'h3456+(2*n+1)*16'h0113;
            if(value!=={want1[7:0],want1[15:8],want0[7:0],want0[15:8]})$fatal(1,"Guest WRITE FIFO wordpair%0d expected%h/%h got%h",n,want0,want1,value);
        end
        legacy(3,5,16'h54e0,0);
        n=0;while(ram_word(4096)!==3 && n<100000)begin tick;n=n+1;end
        // The guest's final status read acknowledges the completion IRQ.
        // Reassert it once the guest is in its terminal loop, so the stock
        // bus's continuously held status-read strobe cannot mask coverage.
        if((actual_fast ? dut.fast_ide_irq : dut.chipset.gayle_irq)!==0)$fatal(1,"Guest status read failed to acknowledge IDE IRQ");
        legacy(3,5,16'h54e0,0);repeat(8)@(negedge chip_clk);
        if((actual_fast ? dut.fast_ide_irq : dut.chipset.gayle_irq)!==1)$fatal(1,"Selected IDE completion IRQ not held after publication");
        if(n==100000 || irq_edges==0 || holds<50)$fatal(1,"Guest completion/IRQ/holds missing marker=%h irq=%0d holds=%0d classicEnable=%b fastEnable=%b",ram_word(4096),irq_edges,holds,dut.chipset.GAYLE1.intena,dut.fastchip_i.gayle.intena);
        if(actual_fast && (fast_reads<256 || classic_reads!=0 || (CPU_MODE==7 && held_reads==0)))$fatal(1,"Fast actual CPU/held CE coverage missing");
        if(!actual_fast && classic_reads==0)$fatal(1,"Classic guest path not exercised");
        // Partial management transfer aborts into a poisoned epoch. A guest
        // reset cannot silently permit another descriptor or a late write.
        fill(2);start_block(3,0);
        while(host_i.block_i.words<8)tick;
        guest_reset=1;
        repeat(80)tick;cr(16'hd100,value);
        partial_words=host_i.block_i.words;
        if(!value[30] || value[28:24]!=4 || value[15:0]!=3 || partial_words>=256)$fatal(1,"Selected-backend partial abort was not fail-closed");
        guest_reset=0;repeat(40)tick;
        if(!host_i.block_i.poisoned || host_i.block_i.words!=partial_words)$fatal(1,"Guest reset released a poisoned block epoch");
        // A fresh PL reset recovers ownership. Change the controller only
        // while the guest is reset and verify management readback follows it.
        guest_reset=1;reset=1;control_resetn=0;repeat(16)tick;
        reset=0;control_resetn=1;repeat(80)tick;
        config_word(8'hf4,16'h001f);
        config_word(8'hf8,(CPU_MODE==0 || CLASSIC) ? 16'h0007:16'h0027);
        if(actual_fast!==(CPU_MODE==0 || CLASSIC))$fatal(1,"Reset-time backend change did not latch");
        legacy(3,2,16'h5aa5,0);legacy(2,2,0,0);
        if(status[15:0]!==16'h5aa5)$fatal(1,"Management readback used stale backend after reset");
        if(actual_fast ? dut.chipset.GAYLE1.ide0.cylinder[15:0]!==16'hffff : dut.fastchip_i.gayle.ide0.cylinder[15:0]!==16'hffff)
            $fatal(1,"Management write reached inactive backend after switch");
        $display("PASS FAST IDE real CPU mode=%0d classic=%0d READ/WRITE512 mixedword/longword/byte IRQ backendbulk holds=%0d heldreads=%0d",CPU_MODE,CLASSIC,holds,held_reads);
        $finish;
    end
    initial begin #100000000;$fatal(1,"Fast IDE native test timed out");end
endmodule
module tb_amiga_fastide_28;tb_amiga_fastide #(.CPU_MODE(3)) test();endmodule
module tb_amiga_fastide_14;tb_amiga_fastide #(.CPU_MODE(7)) test();endmodule
module tb_amiga_fastide_stock;tb_amiga_fastide #(.CPU_MODE(0)) test();endmodule
module tb_amiga_fastide_classic;tb_amiga_fastide #(.CPU_MODE(3),.CLASSIC(1)) test();endmodule
module tb_amiga_fastide_classic14;tb_amiga_fastide #(.CPU_MODE(7),.CLASSIC(1)) test();endmodule

// Gayle's longword writes are two 16-bit strobes. Exercise the separate
// generic IDE io_32 write interface directly to cover both modified bank
// data muxes, including odd pointer positions and all 512 bytes.
module tb_amiga_fastide_fifo32;
    reg clk=0;always #5 clk=~clk;
    reg rst_n=0,io_read=0,io_write=0,io_32=0,mgmt_write=0,mgmt_read=0;
    reg [3:0] io_address=0,mgmt_address=0;
    reg [31:0] io_writedata=0;wire [31:0] io_readdata;
    reg [15:0] mgmt_writedata=0;wire [15:0] mgmt_readdata;
    ide dut(.clk(clk),.rst_n(rst_n),.irq(),.drq(),.request(),.use_fast(1'b0),.no_data(),.drive_en(),
        .io_address(io_address),.io_read(io_read),.io_write(io_write),.io_32(io_32),.io_wait(),
        .io_writedata(io_writedata),.io_readdata(io_readdata),.mgmt_address(mgmt_address),
        .mgmt_write(mgmt_write),.mgmt_read(mgmt_read),.mgmt_writedata(mgmt_writedata),.mgmt_readdata(mgmt_readdata));
    function automatic [15:0] pattern(input integer n);pattern=16'h7c91^(n*16'h0207);endfunction
    task automatic management(input bit writing,input [3:0] address,input [15:0] value);
        @(negedge clk);mgmt_address=address;mgmt_writedata=value;mgmt_write=writing;mgmt_read=!writing;
        @(negedge clk);mgmt_write=0;mgmt_read=0;
        repeat(4)@(negedge clk);
    endtask
    task automatic pio_write(input bit wide,input [31:0] value);
        @(negedge clk);io_32=wide;io_writedata=value;io_write=1;
        repeat(2)@(negedge clk);io_write=0;
        repeat(4)@(negedge clk);
    endtask
    task automatic pio_read(input bit wide,input [31:0] wanted);
        @(negedge clk);io_32=wide;io_read=1;
        repeat(2)@(negedge clk);
        if(wide ? io_readdata!==wanted : io_readdata[15:0]!==wanted[15:0])
            $fatal(1,"Direct io32=%b ptr=%0d expected%h got%h",wide,dut.io_cnt,wanted,io_readdata);
        io_read=0;repeat(4)@(negedge clk);
    endtask
    integer n;reg [15:0] low_word,high_word;
    initial begin
        repeat(8)@(negedge clk);rst_n=1;repeat(8)@(negedge clk);
        management(1,6,16'h0009);management(1,0,16'h0001);management(1,5,16'h5800);
        pio_write(0,{16'd0,pattern(0)});
        for(n=1;n<255;n=n+2)begin
            low_word=pattern(n);high_word=pattern(n+1);pio_write(1,{high_word,low_word});
        end
        pio_write(0,{16'd0,pattern(255)});
        management(0,14,0);
        for(n=0;n<256;n=n+1)begin
            @(negedge clk);mgmt_address=15;mgmt_read=1;
            @(negedge clk);
            if(mgmt_readdata!==pattern(n))$fatal(1,"Direct odd io32 WRITE word%0d expected%h got%h",n,pattern(n),mgmt_readdata);
            mgmt_read=0;repeat(4)@(negedge clk);
        end
        management(1,5,16'h5e00);
        pio_read(0,{16'd0,pattern(0)});
        for(n=1;n<255;n=n+2)begin
            low_word=pattern(n);high_word=pattern(n+1);pio_read(1,{high_word,low_word});
        end
        pio_read(0,{16'd0,pattern(255)});
        if(dut.io_cnt!==256)$fatal(1,"Direct io32 transfer length wrong");
        $display("PASS IDE direct32 odd READ/WRITE256 independent management oracle");$finish;
    end
    initial begin #1000000;$fatal(1,"Direct io32 timeout");end
endmodule

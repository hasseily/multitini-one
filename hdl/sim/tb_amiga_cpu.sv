// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Original diagnostic program executes on the real stock/accelerated CPUs.
// Only the reset timer is shortened. The native bus, clock gate and coherent
// production memory adapter are unmodified. The DDR responder adds variable
// latency so the CPU has to survive real timeline holds.
module tb_amiga_cpu #(parameter integer CPU_MODE=0);
    reg clk=0;always #17.62 clk=~clk;
    reg reset=1,guest_reset=1;
    wire chip_clk,chip_tick,c1,c3,en7,en7n,run_enable;
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
    wire memory_write,memory_valid,memory_busy,memory_fault,memory_burst;
    wire [63:0] memory_wdata;
    wire [7:0] memory_wstrb;
    reg [63:0] response_data=0;
    reg response_valid=0,response_busy=0;
    reg [7:0] response_delay=0;
    reg [23:0] held_addr;
    // Line fills return four ascending beats; every write is one beat.
    reg held_burst=0;reg [1:0] held_beat=0;
    reg held_write;
    reg [63:0] held_data;
    reg [7:0] held_mask;
    reg [63:0] chip_store=0,fast_store=0,blit_store=0,repeat_store=0;
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
    always @(posedge clk)if(!reset && memory_i.lookup &&
        memory_i.request_owner==0 && !memory_i.request_write && memory_i.request_addr==24'h001000)begin
        if(memory_i.hit)dma_source_hits=dma_source_hits+1;
        else if(memory_i.launch_fill)dma_source_misses=dma_source_misses+1;
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
        .ide_address(5'd0),.ide_read(1'b0),.ide_write(1'b0),.ide_wdata(16'd0),
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
        .run_enable(run_enable),.busy(memory_busy),.fault(memory_fault),
        .memory_addr(memory_addr),.memory_write(memory_write),.memory_wdata(memory_wdata),.memory_wstrb(memory_wstrb),
        .memory_burst(memory_burst),.memory_valid(memory_valid),.memory_ready(!response_busy),.memory_rdata(response_data),
        .memory_rvalid(response_valid),.memory_fault(1'b0));

    function automatic [15:0] romword(input [19:0] position);
        case(position)
            0:romword=16'h001f;
            1:romword=16'hf000;
            2:romword=16'h00f8;
            3:romword=16'h0008;
            4:romword=16'h13fc;
            5:romword=16'h0002;
            6:romword=16'h00bf;
            7:romword=16'he001;
            8:romword=16'h33fc;
            9:romword=16'h1234;
            10:romword=16'h0000;
            11:romword=16'h1000;
            12:romword=16'h33fc;
            13:romword=16'h0f00;
            14:romword=16'h00df;
            15:romword=16'hf180;
            16:romword=16'h33fc;
            17:romword=16'h2000;
            18:romword=16'h00e8;
            19:romword=16'h0048;
            20:romword=16'h23fc;
            21:romword=16'h1234;
            22:romword=16'h5678;
            23:romword=16'h0020;
            24:romword=16'h0000;
            25:romword=16'h2039;
            26:romword=16'h0020;
            27:romword=16'h0000;
            28:romword=16'h23c0;
            29:romword=16'h0000;
            30:romword=16'h1004;
            31:romword=16'h23fc;
            32:romword=16'ha1b2;
            33:romword=16'hc3d4;
            34:romword=16'h0020;
            35:romword=16'h0000;
            36:romword=16'h2039;
            37:romword=16'h0020;
            38:romword=16'h0000;
            39:romword=16'h23c0;
            40:romword=16'h0000;
            41:romword=16'h1010;
            42:romword=16'h33fc;
            43:romword=16'h8240;
            44:romword=16'h00df;
            45:romword=16'hf096;
            46:romword=16'h33fc;
            47:romword=16'h09f0;
            48:romword=16'h00df;
            49:romword=16'hf040;
            50:romword=16'h33fc;
            51:romword=16'h0000;
            52:romword=16'h00df;
            53:romword=16'hf042;
            54:romword=16'h33fc;
            55:romword=16'h0000;
            56:romword=16'h00df;
            57:romword=16'hf070;
            58:romword=16'h33fc;
            59:romword=16'h0000;
            60:romword=16'h00df;
            61:romword=16'hf072;
            62:romword=16'h33fc;
            63:romword=16'h0000;
            64:romword=16'h00df;
            65:romword=16'hf074;
            66:romword=16'h33fc;
            67:romword=16'hffff;
            68:romword=16'h00df;
            69:romword=16'hf044;
            70:romword=16'h33fc;
            71:romword=16'hffff;
            72:romword=16'h00df;
            73:romword=16'hf046;
            74:romword=16'h33fc;
            75:romword=16'h0000;
            76:romword=16'h00df;
            77:romword=16'hf050;
            78:romword=16'h33fc;
            79:romword=16'h1000;
            80:romword=16'h00df;
            81:romword=16'hf052;
            82:romword=16'h33fc;
            83:romword=16'h0000;
            84:romword=16'h00df;
            85:romword=16'hf054;
            86:romword=16'h33fc;
            87:romword=16'h1008;
            88:romword=16'h00df;
            89:romword=16'hf056;
            90:romword=16'h33fc;
            91:romword=16'h0000;
            92:romword=16'h00df;
            93:romword=16'hf064;
            94:romword=16'h33fc;
            95:romword=16'h0000;
            96:romword=16'h00df;
            97:romword=16'hf066;
            98:romword=16'h33fc;
            99:romword=16'h0041;
            100:romword=16'h00df;
            101:romword=16'hf058;
            102:romword=16'h3039;
            103:romword=16'h00df;
            104:romword=16'hf002;
            105:romword=16'h0800;
            106:romword=16'h000e;
            107:romword=16'h66f4;
            108:romword=16'h3239;
            109:romword=16'h0000;
            110:romword=16'h1008;
            111:romword=16'h33c1;
            112:romword=16'h0000;
            113:romword=16'h100a;
            114:romword=16'h33fc;
            115:romword=16'h1000;
            116:romword=16'h00df;
            117:romword=16'hf052;
            118:romword=16'h33fc;
            119:romword=16'h100c;
            120:romword=16'h00df;
            121:romword=16'hf056;
            122:romword=16'h33fc;
            123:romword=16'h0041;
            124:romword=16'h00df;
            125:romword=16'hf058;
            126:romword=16'h3039;
            127:romword=16'h00df;
            128:romword=16'hf002;
            129:romword=16'h0800;
            130:romword=16'h000e;
            131:romword=16'h66f4;
            132:romword=16'h3239;
            133:romword=16'h0000;
            134:romword=16'h100c;
            135:romword=16'h33c1;
            136:romword=16'h0000;
            137:romword=16'h100e;
            138:romword=16'h60fe;
            default:romword=16'h4e71;
        endcase
    endfunction
    function automatic [63:0] read_line(input [23:0] address);
        reg [15:0] word_value;integer n;
        begin
            read_line=0;
            if(address==24'h001000)read_line=chip_store;
            else if(address==24'h001008)read_line=blit_store;
            else if(address==24'h001010)read_line=repeat_store;
            else if(address==24'ha00000)read_line=fast_store;
            else if(address>=24'h780000 && address<24'h800000)
                for(n=0;n<4;n=n+1)begin
                    word_value=romword((address-24'h780000)/2+n);
                    read_line[16*n+:16]={word_value[7:0],word_value[15:8]};
                end
        end
    endfunction
    integer lane;
    always @(posedge clk)begin
        response_valid<=0;
        if(!reset && !run_enable)holds<=holds+1;
        if(response_busy)begin
            if(response_delay!=0)response_delay<=response_delay-1'b1;
            else begin
                if(held_write)begin
                    for(lane=0;lane<8;lane=lane+1)if(held_mask[lane])begin
                        if(held_addr==24'h001000)chip_store[lane*8+:8]<=held_data[lane*8+:8];
                        else if(held_addr==24'h001008)blit_store[lane*8+:8]<=held_data[lane*8+:8];
                        else if(held_addr==24'h001010)repeat_store[lane*8+:8]<=held_data[lane*8+:8];
                        else if(held_addr==24'ha00000)fast_store[lane*8+:8]<=held_data[lane*8+:8];
                    end
                end
                response_data<=read_line(held_addr+{held_beat,3'b000});response_valid<=1;
                if(held_burst && held_beat!=2'd3)held_beat<=held_beat+1'b1;else response_busy<=0;
            end
        end else if(memory_valid)begin
            if(transactions<40)$display("DDR tx%0d %h write=%b mask=%h data=%h",transactions,memory_addr,memory_write,memory_wstrb,memory_wdata);
            held_addr<=memory_addr;held_burst<=memory_burst;held_beat<=0;held_write<=memory_write;held_data<=memory_wdata;held_mask<=memory_wstrb;
            response_delay<=4+(transactions%13);response_busy<=1;transactions<=transactions+1;
        end
    end
    task automatic config_cpu;
        @(negedge chip_clk);io_uio=1;io_din=16'h00f4;io_strobe=1;
        @(negedge chip_clk);io_strobe=0;
        @(negedge chip_clk);io_din=CPU_MODE==0 ? 16'h0000 : CPU_MODE==7 ? 16'h005f : 16'h001f;io_strobe=1;
        @(negedge chip_clk);io_strobe=0;
        repeat(8)@(negedge chip_clk);io_uio=0;
        repeat(8)@(negedge chip_clk);
    endtask
    initial begin
        repeat(8)@(negedge clk);reset=0;
        repeat(32)@(negedge chip_clk);config_cpu();
        if(cpucfg[1:0]!==CPU_MODE[1:0])$fatal(1,"CPU fixture configuration mismatch");
        if(cpucfg[3] !== (CPU_MODE==7))$fatal(1,"CPU fixture speed selector mismatch");
        // Shorten only the four-frame reset delay; retain all production CPU
        // reset handling, native bus state and memory scheduling.
        force dut.chipset.CONTROL1.rst_cnt=3'd4;
        guest_reset=0;
        for(cycles=0;cycles<400000;cycles=cycles+1)begin
            @(negedge clk);
            if(memory_fault)$fatal(1,"CPU memory adapter fault");
            if(chip_store[15:0]==16'h3412 && chip_store[63:32]==32'h78563412 &&
               fast_store[31:0]==32'hd4c3b2a1 && repeat_store[31:0]==32'hd4c3b2a1 &&
               blit_store==64'h3412341234123412 && dut.chipset.DENISE1.clut0.clut.mem[0]===32'h0f000f00)begin
                if(holds<20 || transactions<10 || dma_source_hits<1 || dma_source_misses<1)$fatal(1,"CPU fixture did not exercise variable DDR latency");
                if(CPU_MODE!=0 && (kernel_steps<30 || speed_edges<100))$fatal(1,"CPU speed proof did not execute sufficient kernel steps");
                if(CPU_MODE==7 && (speed_opportunities*2-speed_edges>1 || speed_edges-speed_opportunities*2>1))
                    $fatal(1,"14 MHz kernel opportunity count is not exactly half the native rate");
                $display("CPU speed proof mode=%0d native_edges=%0d opportunities=%0d consumed=%0d",CPU_MODE,speed_edges,speed_opportunities,kernel_steps);
                $display("PASS Amiga CPU mode=%0d real instructions, repeated Chip/Fast RAM, native blitter miss/hit DMA (%0d/%0d), COLOR00 and %0d DDR holds/%0d transactions",CPU_MODE,dma_source_misses,dma_source_hits,holds,transactions);
                $finish;
            end
        end
        $fatal(1,"CPU mode=%0d timed out: address=%h chip=%h fast=%h repeat=%h blit=%h reset=%b transactions=%d req=%b raw=%b ready=%b state=%b memory=%0d chipdone=%b cpudone=%b",CPU_MODE,dut.cpu.cpu_addr,chip_store,fast_store,repeat_store,blit_store,cpu_reset_n,transactions,cpu_req,dut.cpu_ram_sel,cpu_ready,cpu_state,memory_i.state,memory_i.chip_done,memory_i.cpu_done);
    end
endmodule
module tb_amiga_cpu_stock;tb_amiga_cpu #(.CPU_MODE(0)) test();endmodule
module tb_amiga_cpu_turbo;tb_amiga_cpu #(.CPU_MODE(3)) test();endmodule
module tb_amiga_cpu_half;tb_amiga_cpu #(.CPU_MODE(7)) test();endmodule

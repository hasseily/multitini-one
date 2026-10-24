// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Actual UA END, CIA1 one-shot and VIC raster IRQ share the production IRQ
// wiring. The real T65 handles them through the exact or accelerated proxy.
module tb_c64_ua_irq;
    reg clk=0,fastclk=0,reset_n=0,pause=1,ntsc=0;
    initial forever begin
        #7.8125;fastclk=1;clk=1;
        #7.8125;fastclk=0;
        #7.8125;fastclk=1;clk=0;
        #7.8125;fastclk=0;
    end
    reg [1:0] bios=0;
    reg rom_we=0,loader_we=0;
    reg [13:0] rom_addr=0;
    reg [7:0] rom_data=0,loader_data=0;
    reg [15:0] loader_addr=0;
    reg [7:0] initial_ram[0:65535];
    integer mode=0,order=0;
    wire [31:0] config_word=mode==0?32'd0:mode==1?32'h45f:32'h41f;
    wire cia_pending,vic_pending,ba;
    wire [2:0] pending={vic_pending,cia_pending,lane[0].dut.test_ua_irq};
    for(genvar g=0;g<1;g=g+1)begin: lane
    c64_turbo_exact_lane #(.EXTERNAL(1)) dut(
        .test_config(config_word),.test_ua_enable(1'b1),
        .e4_cia_pending(cia_pending),.e4_vic_pending(vic_pending),
        .clk(clk),.fastclk(fastclk),.reset_n(reset_n),.pause(pause),.memory_wait(1'b0),
        .ntsc(ntsc),.irq_n(1'b1),.nmi_n(1'b1),.bios(bios),
        .rom_we(rom_we),.rom_addr(rom_addr),.rom_data(rom_data),
        .loader_we(loader_we),.loader_addr(loader_addr),.loader_data(loader_data),
        .dma_req(1'b0),.dma_we(1'b0),.dma_addr(16'd0),.dma_data(8'd0),.dbg_rdy(ba)
    );
    end
    integer cases=0,markers=0,entries=0,ua_acks=0,low_acks=0,reply_checks=0;
    integer timeout_ticks,rows,expected_mask=7;
    reg measuring=0,finished=0,ua_reply_pending=0;
    wire island_enable=lane[0].dut.accelerated.cpu_island_i.cpu_enable;
    wire island_write=lane[0].dut.accelerated.cpu_island_i.cpu_write;
    wire [15:0] island_addr=lane[0].dut.accelerated.cpu_island_i.cpu_address;
    wire [7:0] island_data=lane[0].dut.accelerated.cpu_island_i.cpu_wdata;
    always @(posedge clk) begin
        if(measuring && lane[0].dut.ext_cpu_bus_valid && lane[0].dut.ext_cpu_we &&
            lane[0].dut.ext_cpu_addr==16'hdf3f && lane[0].dut.ext_cpu_do==1) begin
            ua_acks++;
            if(!ba)low_acks++;
            if(mode!=0)ua_reply_pending=1;
        end
    end
    always @(posedge fastclk) begin
        if(measuring && island_enable) begin
            if(ua_reply_pending) begin
                if(lane[0].dut.test_ua_irq!==0 ||
                    lane[0].dut.accelerated.cpu_island_i.irq_q!==lane[0].dut.ext_irq_n)
                    $fatal(1,"UA clear completed before IRQ release ntsc=%0d mode=%0d order=%0d",ntsc,mode,order);
                reply_checks++;ua_reply_pending=0;
            end
            if(island_write && island_addr==16'h2110) begin
                if(pending!==island_data[2:0] || lane[0].dut.ext_irq_n!==!(|pending))
                    $fatal(1,"IRQ source isolation mask=%h expected=%h combined_n=%b ntsc=%0d mode=%0d order=%0d",pending,island_data[2:0],lane[0].dut.ext_irq_n,ntsc,mode,order);
                if(markers==0 && pending!=7)$fatal(1,"Missing simultaneous IRQ sources");
                expected_mask=island_data[2:0];markers++;
                $fdisplay(rows,"%0d,%0d,%0d,%0d,%0d,%0d,%0d",ntsc,mode,order,markers,pending,lane[0].dut.ext_irq_n,ba);
            end
            if(island_write && island_addr==16'h2101) begin
                entries++;
                if(island_data!=1)$fatal(1,"Unexpected repeated interrupt handler");
            end
            if(island_write && island_addr==16'h2105 && island_data==8'ha5)finished=1;
        end
    end
    task automatic load_all;
        @(negedge clk);loader_we=1;
        for(integer a=0;a<65536;a++)begin loader_addr=a;loader_data=initial_ram[a];@(negedge clk);end
        loader_we=0;bios=0;rom_we=1;
        for(integer a=0;a<16384;a++)begin
            rom_addr=a;rom_data=a==14'h3ffc ? 8'h00 : a==14'h3ffd ? 8'h08 : 8'hea;@(negedge clk);
        end
        bios=1;
        for(integer a=0;a<4096;a++)begin rom_addr=a;rom_data=8'hff;@(negedge clk);end
        rom_we=0;bios=0;
    endtask
    initial begin
        string file_name;
        rows=$fopen("irq_sources.csv","w");
        $fdisplay(rows,"ntsc,mode,order,marker,pending,irq_n,ba");
        for(integer standard=0;standard<2;standard++)
        for(integer speed=0;speed<3;speed++)
        for(integer permutation=0;permutation<3;permutation++)begin
            measuring=0;reset_n=0;pause=1;ntsc=standard;mode=speed;order=permutation;
            file_name=$sformatf("irq-%0d.hex",order);$readmemh(file_name,initial_ram);load_all();
            repeat(160)@(negedge clk);
            markers=0;entries=0;ua_acks=0;low_acks=0;reply_checks=0;expected_mask=7;
            finished=0;ua_reply_pending=0;measuring=1;reset_n=1;pause=0;timeout_ticks=0;
            // Guest reset preserves raster phase, so allow the next frame's
            // raster-51 interrupt when a preceding case ended after line 51.
            while(!finished && timeout_ticks<800000)begin @(negedge clk);timeout_ticks++;end
            if(!finished)$fatal(1,"UA IRQ integration timeout ntsc=%0d mode=%0d order=%0d mask=%h addr=%h",ntsc,mode,order,pending,island_addr);
            repeat(128)@(negedge clk);measuring=0;
            if(markers!=4 || entries!=1 || ua_acks!=1 || pending!=0 || expected_mask!=0)
                $fatal(1,"Incomplete IRQ integration markers=%0d entries=%0d acks=%0d pending=%h",markers,entries,ua_acks,pending);
            if(lane[0].dut.island_turbo!==(mode!=0))$fatal(1,"Requested speed was not active");
            if(mode!=0 && (lane[0].dut.accelerated.cpu_island_i.active_index!=15 || reply_checks!=1))
                $fatal(1,"Accelerated IRQ completion not covered");
            if(mode==2 && low_acks==0)$fatal(1,"BA-low UA acknowledgement not covered");
            if(lane[0].dut.test_ua.fetch_count==0 || lane[0].dut.test_ua.underrun_count!=0)
                $fatal(1,"UA END did not use a ready sample");
            $display("UA_IRQ_SUMMARY ntsc=%0d mode=%0d order=%0d markers=%0d low_acks=%0d replies=%0d",ntsc,mode,order,markers,low_acks,reply_checks);
            cases++;
        end
        $fclose(rows);$display("UA_IRQ PASS cases=%0d",cases);$finish;
    end
    initial begin #1000000000;$fatal(1,"UA IRQ global timeout");end
endmodule

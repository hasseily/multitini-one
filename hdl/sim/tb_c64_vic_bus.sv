// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Production VIC, with observation-only ports added by the runner. Enables
// use the unpaused FPGA64 32-phase convention: pixels at 3+4n, data at 15/31.
module tb_c64_vic_bus;
    logic clk=0, reset=1;
    integer phase=0, ntsc=0, scenario=0;
    wire phi=phase>=16;
    wire ena_pixel=(phase%4)==3;
    wire ena_data=phase==15 || phase==31;
    logic cs=0, we=0;
    logic [5:0] reg_addr=0;
    logic [7:0] reg_data=0;
    wire ba, addr_valid, badline;
    wire [3:0] state;
    wire [2:0] sprite;
    wire [8:0] raster;
    wire [9:0] raw_x;
    wire [7:0] dma;
    integer fd, cycle=11, beam=0, anchors=0, count=0, cycles, lines;
    string trace_path;
    logic aligned=0, recording=0;
    logic [7:0] configuration [0:1];
    always #5 clk=!clk;
    always @(negedge clk) phase=(phase+1)%32;
    video_vicii_656x #(.registeredAddress(1), .emulateRefresh(1),
        .emulateLightpen(1), .emulateGraphics(1)) dut (
        .clk(clk), .phi(phi), .enaData(ena_data), .enaPixel(ena_pixel),
        .baSync(1'b0), .ba(ba), .ba_dma(), .mode6569(!ntsc),
        .mode6567old(1'b0), .mode6567R8(ntsc!=0), .mode6572(1'b0),
        .turbo_en(1'b0), .turbo_state(), .variant(2'b00), .reset(reset),
        .cs(cs), .we(we), .lp_n(1'b1), .aRegisters(reg_addr),
        .diRegisters(reg_data), .di(8'd0), .diColor(4'd0), .\do (),
        .vicAddr(), .irq_n(), .hSync(), .vSync(), .colorIndex(),
        .debugX(), .debugY(), .vicRefresh(), .addrValid(addr_valid),
        .observe_state(state), .observe_sprite(sprite), .observe_raster(raster),
        .observe_x(raw_x), .observe_dma(dma), .observe_badline(badline)
    );
    task automatic write_register(input integer address, input integer value);
        while(phase!=16) @(negedge clk);
        cs=1; we=1; reg_addr=address; reg_data=value;
        // Keep the bus stable through the subsequent phi1 register latches.
        repeat(32) @(negedge clk);
        cs=0; we=0;
        repeat(32) @(negedge clk);
    endtask
    always @(posedge clk) begin
        if (phase==0) begin
            if (!aligned && state==0) begin
                // cycleRefresh1 is the first refresh fetch: hardware cycle 11.
                aligned=1; cycle=11; beam=raster;
            end else if(aligned) begin
                if(cycle==cycles) begin cycle=1; beam=(beam+1)%lines; end
                else cycle++;
            end
            if(aligned && beam==0 && cycle==11 && !reset) begin
                anchors++;
                if(anchors==2) recording=1;
            end
        end
        if(recording && phase==28) begin
            if($isunknown({ba,addr_valid,raster,dma,badline}))
                $fatal(1,"FAIL VIC trace unknown ownership state");
            // addrValid is VIC ownership, the inverse of physical AEC.
            $fdisplay(fd,"%0d,%0d,%0d,%0d,%0d,%0d,%0d",beam,cycle,raster,ba,!addr_valid,dma,badline);
            count++;
            if(count==cycles*lines) begin
                $fclose(fd);
                $display("PASS production VIC ownership ntsc=%0d scenario=%0d cycles=%0d",ntsc,scenario,count);
                $finish;
            end
        end
    end
    initial begin
        $readmemh("case.hex",configuration);
        ntsc=configuration[0]; scenario=configuration[1];
        cycles=ntsc ? 65 : 63; lines=ntsc ? 263 : 312;
        trace_path="rtl_trace.csv";
        fd=$fopen(trace_path,"w");
        if(fd==0) $fatal(1,"FAIL VIC trace file could not be opened");
        $fdisplay(fd,"line,cycle,raster,ba,aec,dma,badline");
        repeat(64) @(negedge clk);
        reset=0;
        write_register('h11,scenario==0 || scenario==4 || scenario==5 ? 0 : 'h18 | (scenario==2 ? 3 : scenario==3 ? 7 : 0));
        for(integer i=0;i<8;i++)
            write_register(1+2*i,scenario==7 ? 72+3*i : scenario==10 ? 255 : scenario==11 ? 0 : 80);
        write_register('h17,scenario==8 ? 'hff : scenario==9 ? 'h55 : 0);
        write_register('h15,scenario<4 ? 0 : scenario==4 ? 1 : scenario==5 ? 'h80 : 'hff);
        repeat(4*cycles*lines*32) @(negedge clk);
        $fatal(1,"FAIL VIC trace frame timeout");
    end
endmodule

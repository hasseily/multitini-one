// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps

// Complete-frame native reference for exact-mode preservation.  observe_state
// is added to an ignored copy by the runner; the executable VIC body is exact.
// A rolling pixel digest covers all pixel edges between native trace samples.
module tb_c64_turbo_vic_native;
    logic clk=0,reset=1; integer phase=0,ticks=0,ntsc,cycles,lines,fd;
    integer wraps=0,last_raster=0,samples=0,recording=0;
    logic [7:0] config_bytes[0:1];
    logic cs=0,we=0;logic [5:0] address=0;logic [7:0] data=0;
    wire phi=phase>=16,ena_data=(phase==15||phase==31),ena_pixel=(phase%4==3);
    wire [13:0] vic_addr;wire[7:0] reg_data;
    wire ba,ba_dma,ba_reu,irq_n,hs,vs,valid;
    wire[3:0] color;wire[9:0] debug_x;wire[8:0] debug_y;
    wire[511:0] observe_state;
    logic[31:0] pixels=32'h811c9dc5;
    wire[7:0] memory_data=vic_addr[7:0]^vic_addr[13:6]^8'ha5;
    always #5 clk=~clk;
    video_vicii_656x #(.registeredAddress(1),.emulateRefresh(1),.emulateLightpen(1),.emulateGraphics(1)
`ifdef TURBO_PORTS
        ,.FAST_PORT(1)
`endif
    ) dut (
        .clk(clk),.reset(reset),.phi(phi),.enaData(ena_data),.enaPixel(ena_pixel),
        .baSync(1'b0),.ba(ba),.ba_dma(ba_dma),.ba_reu(ba_reu),
        .mode6569(!ntsc),.mode6567old(1'b0),.mode6567R8(ntsc!=0),.mode6572(1'b0),
        .turbo_en(1'b0),.turbo_state(),.variant(2'b00),.char_rom_bank(1'b1),
        // Hold a real light-pen input active. Its power-on latch has no reset;
        // the first frame wrap arms it, and the second settling frame lets
        // native LPX/LPY reads establish the registered readback value.
        .read_early_active(1'b0),.read_early(1'b0),.cs(cs),.we(we),.lp_n(1'b0),
        .aRegisters(address),.diRegisters(data),.di(memory_data),.diColor(vic_addr[3:0]),
        .\do (reg_data),.vicAddr(vic_addr),.irq_n(irq_n),.hSync(hs),.vSync(vs),
        .colorIndex(color),.debugX(debug_x),.debugY(debug_y),.vicRefresh(),.addrValid(valid),
        .observe_state(observe_state)
`ifdef TURBO_PORTS
        ,.fast_phase(5'(phase)),.fast_rd_addr(6'd0),.fast_rd_data(),
        .fast_wr(1'b0),.fast_wa(6'd0),.fast_wd(8'd0),
        .fast_clr_m2m(1'b0),.fast_clr_m2d(1'b0),.fast_hold_d011(),.fast_hold_spr(),.fast_native_busy()
`endif
    );
    task automatic access_at(input integer t);
        integer slot;
        slot=t%128;address=slot%64;data=8'((t*13)^(t>>7));
        // Repeated sprite initialization and mode/raster storms exercise both
        // frame wraps, badlines, sprite start/end, IRQ and collision history.
        if(slot<16) data=(slot%2)?8'(70+(slot/2)*13):8'(48+(slot/2)*23);
        case(slot%64)
            'h10:data=0; 'h11:data=((t/128)%5==0)?8'h3b:8'h1b;
            'h12:data=8'(80+(t/128)%140); 'h15:data=8'hff;
            'h16:data=8'h18; 'h17:data=8'h55; 'h18:data=8'h14;
            'h19:data=8'h0f; 'h1a:data=8'h0f; 'h1b:data=8'h0f;
            'h1c:data=8'h55; 'h1d:data=8'haa;
            default:begin end
        endcase
        // Each access owns native phi2.  Collision and read-only locations
        // receive native reads so read-clear tails are included in goldens.
        cs=1;we=!(address=='h1e||address=='h1f||address=='h13||address=='h14);
    endtask
    always @(negedge clk) begin
        phase=(phase+1)%32;
        if(!reset) begin
            if(phase==0)begin ticks++;cs=0;we=0;end
            if(phase==16)access_at(ticks);
        end else begin cs=0;we=0;end
    end
    always @(posedge clk) if(!reset) begin
        // Pre-edge observation agrees with the T2 comparison convention.
        if(phase==15) begin
            if(debug_y<last_raster)wraps++;
            last_raster=debug_y;
            if(wraps==2 && !recording) begin recording=1;pixels=32'h811c9dc5;end
            if(recording) begin
                if($isunknown({observe_state,vic_addr,ba,ba_dma,ba_reu,irq_n,reg_data,color}))
                    $fatal(1,"unknown settled VIC state state=%h addr=%h ba=%b/%b/%b irq=%b reg=%h color=%h",observe_state,vic_addr,ba,ba_dma,ba_reu,irq_n,reg_data,color);
                $fdisplay(fd,"%0d,%0128x,%04x,%01x,%02x,%08x",samples,observe_state,vic_addr,
                          {ba,ba_dma,ba_reu,valid},{4'd0,irq_n,hs,vs,1'b0},pixels);
                samples++;
                if(samples==2*cycles*lines) begin
                    $fclose(fd);$display("PASS VIC native frames=2 samples=%0d mode=%0d",samples,ntsc);$finish;
                end
            end
        end
        if(ena_pixel)begin #1;pixels=(pixels^{28'd0,color})*32'h01000193;end
    end
    initial begin
        $readmemh("case.hex",config_bytes);ntsc=config_bytes[0];cycles=ntsc?65:63;lines=ntsc?263:312;
        fd=$fopen("native.csv","w");if(!fd)$fatal(1,"cannot open native VIC trace");
        $fdisplay(fd,"sample,state,address,bus,sync,pixel_hash");
        repeat(96)@(negedge clk);#1;reset=0;
    end
    initial begin repeat(5*312*65*32)@(posedge clk);$fatal(1,"VIC native timeout");end
endmodule

// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps

// Exact-mode regression stimulus for the fast-port work.  The real synthesis
// leaves run for three complete native frame durations.  The first settles
// power-on state; the following two are retained, including every native
// audio sample and the SID read latch.  No ROM, player or emulator is used.
module tb_c64_turbo_sid_native;
    logic clk=0, reset=1;
    integer phase=0, ticks=0, samples=0, frame_cycles, fd, model, ntsc;
    logic [7:0] config_bytes[0:1];
    logic [1:0] cs=0;
    logic we=0;
    logic [4:0] addr=0;
    logic [7:0] data_in=0;
    wire [7:0] data_out;
    wire [17:0] audio_l,audio_r;
    wire ce_1m=(phase==0);
    always #5 clk=~clk;

    sid_top #(.DUAL(1),.MULTI_FILTERS(1)
`ifdef TURBO_PORTS
        ,.FAST_PORT(1),.EDGE_STAGE(1)
`endif
    ) dut (
        .clk(clk),.reset(reset),.ce_1m(ce_1m),.cs(cs),.we(we),
        .addr(addr),.data_in(data_in),.data_out(data_out),
        .fc_offset_l(13'd0),.fc_offset_r(13'd0),
        .pot_x_l(8'h12),.pot_y_l(8'h34),.pot_x_r(8'h56),.pot_y_r(8'h78),
        .ext_in_l(18'd0),.ext_in_r(18'd0),.audio_l(audio_l),.audio_r(audio_r),
        .filter_en(2'b11),.mode(model?2'b11:2'b00),.cfg(4'd0),
        .ld_clk(clk),.ld_addr(12'd0),.ld_data(16'd0),.ld_wr(1'b0)
`ifdef TURBO_PORTS
        ,.fio_cs(2'b00),.fio_we(1'b0),.fio_latch(1'b0),.fio_latch_src(2'b00),
        .fio_addr(5'd0),.fio_wdata(8'd0),.fast_rd_cs(2'b00),
        .fast_rd_addr(5'd0),.fast_rd_data()
`endif
    );

    task automatic set_access(input integer t);
        integer slot;
        // Only native writes, at phase 29, with chip select retained until
        // phase 31.  In particular $19-$1C writes see the subsequent native
        // read/latch behavior, rather than an artificial single-edge select.
        slot=t%256; addr=slot%32; data_in=8'((t*37)^(t>>8));
        cs=(slot<32)?2'b01:((slot<64)?2'b10:2'b11);
        case(slot%32)
            0: data_in=8'h31; 1: data_in=(cs==2'b10)?8'h0b:8'h24;
            2: data_in=8'h00; 3: data_in=8'h08;
            4: data_in=((t/256)%4==0)?8'h40:8'h41;
            5: data_in=8'h12; 6: data_in=8'hb3;
            7: data_in=8'h62; 8: data_in=8'h17;
            9: data_in=8'h30; 10: data_in=8'h06;
            11: data_in=((t/256)%5==0)?8'h29:8'h21;
            12: data_in=8'h23; 13: data_in=8'ha4;
            14: data_in=8'h93; 15: data_in=8'h09;
            16: data_in=8'h10; 17: data_in=8'h04;
            18: data_in=((t/256)%3==0)?8'h11:8'h81;
            19: data_in=8'h01; 20: data_in=8'hc5;
            21: data_in=8'h04; 22: data_in=8'h58;
            23: data_in=8'((t/256)%8);
            24: data_in=((t/256)%2)?8'h1f:8'h0f;
            default: data_in=8'((t*37)^(t>>8));
        endcase
    endtask

    always @(negedge clk) begin
        phase=(phase+1)%32;
        if(!reset) begin
            if(phase==0) ticks++;
            if(phase==16) set_access(ticks);
            we=(phase==29);
            if(phase==0) cs=0;
        end else begin cs=0;we=0;end
    end

    always @(posedge clk) if(!reset && phase==15 && ticks>=frame_cycles) begin
        if($isunknown({audio_l,audio_r,data_out,dut.Misc_Osc3[0],dut.Misc_Osc3[1],
                       dut.Misc_Env3[0],dut.Misc_Env3[1]}))
            $fatal(1,"unknown settled SID state");
        $fdisplay(fd,"%0d,%05x,%05x,%02x,%02x,%02x,%02x,%02x,%02x,%02x",
            samples,audio_l,audio_r,dut.bus_data[0],dut.bus_data[1],
            dut.Misc_Osc3[0],dut.Misc_Osc3[1],dut.Misc_Env3[0],dut.Misc_Env3[1],data_out);
        samples++;
        if(samples==2*frame_cycles) begin
            $fclose(fd);
            $display("PASS SID native frames=2 samples=%0d mode=%0d model=%0d",samples,ntsc,model);
            $finish;
        end
    end

    initial begin
        $readmemh("case.hex",config_bytes);ntsc=config_bytes[0];model=config_bytes[1];
        frame_cycles=ntsc?65*263:63*312;
        fd=$fopen("native.csv","w");if(!fd)$fatal(1,"cannot open native SID trace");
        $fdisplay(fd,"sample,left,right,latch_l,latch_r,osc_l,osc_r,env_l,env_r,bus");
        repeat(96)@(negedge clk);#1;reset=0;
    end
    initial begin repeat(4*312*65*32)@(posedge clk);$fatal(1,"SID native timeout");end
endmodule

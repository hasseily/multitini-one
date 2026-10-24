// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_c64_sid_array;
    reg clk=0;always #5 clk=~clk;
    reg reset=1,map_enable=0,io_select=0,we=0,fio_we=0,fio_latch=0;
    reg [4:0] phase=0;always @(posedge clk)phase<=phase+1'b1;
    wire ce_1m=phase==0;
    reg [1:0] cs=0,fio_cs=0,fio_latch_src=0,fast_rd_cs=0;
    reg [11:0] addr=0;reg [9:0] fio_addr=0,fast_rd_addr=0;
    reg [7:0] data_in=0,fio_wdata=0;wire [7:0] data_out,fast_rd_data;
    reg [255:0] slot_masks=0,chip_params=0;reg [95:0] owners=0;
    reg [31:0] write_inhibit=0;reg [127:0] io_masks=0;wire [143:0] chip_audio;
    wire [17:0] audio_l,audio_r;
    multitini_c64_sid_array #(.SID_COUNT(8),.FAST_PORT(1)) dut(
        .clk(clk),.reset(reset),.ce_1m(ce_1m),.cs(cs),.we(we),.addr(addr),.data_in(data_in),.data_out(data_out),
        .fio_cs(fio_cs),.fio_we(fio_we),.fio_latch(fio_latch),.fio_latch_src(fio_latch_src),.fio_addr(fio_addr),.fio_wdata(fio_wdata),
        .fast_rd_cs(fast_rd_cs),.fast_rd_addr(fast_rd_addr),.fast_rd_data(fast_rd_data),
        .fc_offset_l(13'd0),.fc_offset_r(13'd0),.pot_x_l(8'h12),.pot_y_l(8'h34),.pot_x_r(8'hff),.pot_y_r(8'hff),
        .ext_in_l(18'd0),.ext_in_r(18'd0),.audio_l(audio_l),.audio_r(audio_r),.filter_en(2'b11),.mode(2'b00),.cfg(4'b0),
        .ld_clk(clk),.ld_addr(12'd0),.ld_data(16'd0),.ld_wr(1'b0),.map_enable(map_enable),.io_select(io_select),
        .slot_masks(slot_masks),.write_inhibit(write_inhibit),.owners(owners),.io_masks(io_masks),.chip_params(chip_params),.chip_audio(chip_audio));
    integer checks=0,s,n,r,m,t,f,a;
    integer audio_min[8],audio_max[8],audio_different[8];
    reg [7:0] expected[8];
    task automatic check(input bit ok,input string why);checks++;if(!ok)$fatal(1,"SID ARRAY %s addr=%x data=%x expected=%x",why,addr,data_out,expected[0]);endtask
    task automatic step;@(posedge clk);#1;endtask
    task automatic native_write(input integer slot,input [7:0] value);
        @(negedge clk);addr=12'h400+slot*32;cs=1;we=1;data_in=value;step();@(negedge clk);cs=0;we=0;
    endtask
    task automatic audio_register(input integer chip,input [4:0] regno,input [7:0] value);
        @(negedge clk);addr=12'h400+chip*32+regno;cs=1;we=1;data_in=value;
        step();@(negedge clk);cs=0;we=0;
    endtask
    task automatic read_all;
        for(integer j=0;j<8;j++)begin
            @(negedge clk);addr=12'h400+j*32;cs=1;we=0;fast_rd_addr=j*32;step();
            check(data_out===expected[j],"native separate bus latches");check(fast_rd_data===expected[j],"fast owner read");
        end
        @(negedge clk);cs=0;
    endtask
    initial begin
        for(s=0;s<32;s++)begin slot_masks[s*8+:8]=1<<(s%8);owners[s*3+:3]=s%8;end
        repeat(4)step();while(phase!=8)step();@(negedge clk);reset=0;map_enable=1;
        // The first register write before the first native CE must survive.
        native_write(7,8'h69);fast_rd_addr=7*32;#1;
        check(fast_rd_data===8'h69,"initial map write before first engine CE");
        repeat(64)step();
        for(m=0;m<2;m++)begin
            for(n=0;n<8;n++)chip_params[n*32+16]=m;
            for(n=0;n<8;n++)begin expected[n]=8'h40+n+16*m;native_write(n,expected[n]);end
            read_all();
            for(n=0;n<8;n++)begin
                @(negedge clk);fio_cs=1;fio_we=1;fio_addr=n*32;fio_wdata=8'ha0+n+16*m;
                expected[n]=fio_wdata;step();@(negedge clk);fio_cs=0;fio_we=0;
            end
            read_all();
            for(n=0;n<8;n++)begin
                @(negedge clk);addr=12'h419+n*32;cs=1;fast_rd_addr=10'h019+n*32;step();
                check(data_out===(n==0?8'h12:8'hff),"native paddle owner");check(fast_rd_data===(n==0?8'h12:8'hff),"fast paddle owner");
                expected[n]=n==0?8'h12:8'hff;
            end
            @(negedge clk);cs=0;read_all();
            // Shared read fan-out retains each SID's own live value.
            slot_masks[7:0]=8'hff;owners[2:0]=0;
            @(negedge clk);fio_cs=1;fio_latch=1;fio_addr=10'h019;fio_wdata=8'h12;step();
            @(negedge clk);fio_cs=0;fio_latch=0;slot_masks[7:0]=1;
            read_all();
            write_inhibit[4]=1;native_write(4,8'h66);
            @(negedge clk);fio_cs=1;fio_we=1;fio_addr=4*32;fio_wdata=8'h77;step();
            @(negedge clk);fio_cs=0;fio_we=0;read_all();write_inhibit[4]=0;
            // External I/O is explicitly selected; no hidden D400 fallback.
            io_masks[0+:8]=8'h80;
            @(negedge clk);io_select=1;addr=12'he00;we=1;data_in=8'hc7;step();
            @(negedge clk);io_select=0;we=0;expected[7]=8'hc7;read_all();
            @(negedge clk);io_select=1;addr=12'he20;step();check(data_out===8'hff,"unmapped IO inert read");
            @(negedge clk);io_select=0;
        end
        // Independent oscillator/envelope live reads from all eight engines.
        for(n=0;n<8;n++)begin
            for(r=0;r<7;r++)begin
                @(negedge clk);cs=1;we=1;addr=12'h40e+n*32+r;
                case(r)0:data_in=8'h11+n;1:data_in=8'h20+n;2:data_in=0;3:data_in=8'h08;4:data_in=8'h21;5:data_in=0;default:data_in=8'hf0;endcase
                step();
            end
        end
        @(negedge clk);cs=0;we=0;repeat(4096)step();
        for(n=0;n<8;n++)begin
            fast_rd_addr=n*32+5'h1c;#1;check(fast_rd_data>0,"each envelope advances natively");
            fast_rd_addr=n*32+5'h1b;#1;check(!$isunknown(fast_rd_data),"each oscillator is defined");
        end
        // Exercise actual per-chip audio, not synthetic mixer inputs. Distinct
        // oscillator frequencies must survive both direct and low-pass paths.
        for(m=0;m<2;m++)for(f=0;f<2;f++)begin
            for(n=0;n<8;n++)begin
                chip_params[n*32+16]=m;
                audio_register(n,5'h15,0);audio_register(n,5'h16,8'h60);
                audio_register(n,5'h17,f?8'h04:0);
                audio_register(n,5'h18,f?8'h1f:8'h0f);
                audio_min[n]=131071;audio_max[n]=-131072;audio_different[n]=0;
            end
            for(t=0;t<4096;t++)begin
                while(phase!=31)step();step();
                if(t>=2048)for(n=0;n<8;n++)begin
                    check(!$isunknown(chip_audio[n*18+:18]),"each real audio output defined");
                    a=$signed(chip_audio[n*18+:18]);
                    if(a<audio_min[n])audio_min[n]=a;
                    if(a>audio_max[n])audio_max[n]=a;
                    if(n!=0 && chip_audio[n*18+:18]!=chip_audio[17:0])audio_different[n]++;
                end
            end
            for(n=0;n<8;n++)begin
                check(audio_min[n]<audio_max[n],"each direct/filtered engine has live waveform");
                if(n!=0)check(audio_different[n]>0,"independent engine audio is not broadcast");
            end
            $display("SID EIGHT AUDIO model=%0d filtered=%0d samples=2048 chips=8",m,f);
        end
        $display("PASS SID ARRAY checks=%0d",checks);$finish;
    end
    initial begin #10000000;$fatal(1,"SID array timeout");end
endmodule

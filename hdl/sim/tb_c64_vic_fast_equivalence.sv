// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// DUT writes pass through the real queue consumer. The batch reference uses
// only the pinned stock register-write cases at their original native edges.
module tb_c64_vic_fast_equivalence;
    reg clk=0,reset=1;always #5 clk=~clk;
    reg[7:0] config_bytes[4];integer phase=0,tick=0,cycle=0,ntsc=0,replay=0,mutant=0;
    wire phi=phase>=16,ena_pixel=phase%4==3,ena_data=phase==15||phase==31;
    reg[87:0] entries=0;reg[2:0]wp=0;wire[2:0]done;
    wire wr,clear_m,clear_d,h1,h2,busy;wire[5:0]wa;wire[7:0]wd;
    wire[511:0]state_dut,state_ref;wire[8:0]y,y_ref;wire[9:0]x,x_ref;
    wire[13:0]address_dut,address_ref;wire ba_reu,ba_ref,aec_dut,aec_ref;
    wire[7:0]unused_read;
    reg[576:0] mapped[0:65535];
    wire[576:0] map_word=replay ? mapped[cycle+(phase>=16)] : 577'd0;
    reg lp=1,last_lp=1;integer events=0,trace_file,event_file,frames=0,last_y=0,requests=0;
    integer retain_edges=0,trace_y=-1;
    reg[31:0] random_state=32'hc641fade;
    multitini_c64_fio_port queue_i(.clk(clk),.reset(reset),.fio_entries(entries),.fio_wp(wp),.fio_done(done),
        .vic_busy(busy),.vic_hold_d011(mutant==1?1'b0:h1),
        .vic_hold_spr(mutant==2?1'b0:(mutant==3&&state_dut[3:0]==8&&phase==13?1'b0:h2)),
        .vic_wr(wr),.vic_clr_m2m(clear_m),.vic_clr_m2d(clear_d),.vic_wa(wa),.vic_wd(wd),
        .sid_wr(),.sid_latch(),.sid_a(),.sid_d(),.u64_wr(),.u64_a(),.u64_d());
    video_vicii_656x #(.registeredAddress(1),.emulateRefresh(1),.emulateLightpen(1),.emulateGraphics(1),.FAST_PORT(1)) dut(
        .clk(clk),.reset(reset),.phi(phi),.enaData(ena_data),.enaPixel(ena_pixel),.baSync(1'b0),.ba(),.ba_dma(),.ba_reu(ba_reu),
        .mode6569(!ntsc),.mode6567old(1'b0),.mode6567R8(ntsc!=0),.mode6572(1'b0),.turbo_en(1'b0),.turbo_state(),.variant(2'b00),.char_rom_bank(1'b1),
        .read_early_active(1'b0),.read_early(1'b0),.cs(1'b0),.we(1'b0),.lp_n(lp),.aRegisters(6'd0),.diRegisters(8'd0),.di(aec_dut?8'ha5:8'hff),.diColor(4'hf),.\do (),
        .vicAddr(address_dut),.irq_n(),.hSync(),.vSync(),.colorIndex(),.debugX(x),.debugY(y),.vicRefresh(),.addrValid(aec_dut),.observe_state(state_dut),
        .fast_phase(5'(phase)),.fast_rd_addr(6'd0),.fast_rd_data(unused_read),.fast_wr(wr),.fast_wa(wa),.fast_wd(wd),
        .fast_clr_m2m(clear_m),.fast_clr_m2d(clear_d),.fast_hold_d011(h1),.fast_hold_spr(h2),.fast_native_busy(busy));
    video_vicii_656x_reference #(.registeredAddress(1),.emulateRefresh(1),.emulateLightpen(1),.emulateGraphics(1)) reference_i(
        .clk(clk),.reset(reset),.phi(phi),.enaData(ena_data),.enaPixel(ena_pixel),.baSync(1'b0),.ba(),.ba_dma(),.ba_reu(ba_ref),
        .mode6569(!ntsc),.mode6567old(1'b0),.mode6567R8(ntsc!=0),.mode6572(1'b0),.turbo_en(1'b0),.turbo_state(),.variant(2'b00),.char_rom_bank(1'b1),
        .read_early_active(1'b0),.read_early(1'b0),.cs(1'b1),.we(1'b1),.lp_n(lp),.aRegisters(6'd0),.diRegisters(8'd0),.di(aec_ref?8'ha5:8'hff),.diColor(4'hf),.\do (),
        .vicAddr(address_ref),.irq_n(),.hSync(),.vSync(),.colorIndex(),.debugX(x_ref),.debugY(y_ref),.vicRefresh(),.addrValid(aec_ref),.observe_state(state_ref),
        .replay_mask(map_word[575:512]),.replay_data(map_word[511:0]),.replay_den_enable(replay&&phase==3&&mutant!=4),.replay_den(map_word[576]));
    task automatic enqueue(input reg[5:0]a,input reg[7:0]d);
        if(3'(wp-done)<4)begin entries[(wp&3)*22+:22]={1'b0,2'b00,1'b0,4'd0,a,d};wp=wp+1'b1;requests++;end
    endtask
    always @(posedge clk) if(!reset)begin
        if(wr)begin $fdisplay(event_file,"%0d,%0d,%0d,%0d,%0d",tick,phase,y,wa,wd);events++;end
        // Pre-edge state: the fast and stock machines have not consumed this
        // edge yet. Sticky checking keeps all edges, including short pulses.
        if(wr||y!=trace_y||lp!=last_lp)retain_edges=3;
        if(retain_edges>0||phase==0||phase==11||phase==15||phase==16||phase==27)
            $fdisplay(trace_file,"%0d,%0d,%0d,%0d,%0d,%0128h,%0128h,%04h,%04h,%b,%b,%b,%b,%b,%b,%b,%b,%b,%b",tick,phase,y,reset,lp,state_dut,state_ref,address_dut,address_ref,ba_reu,ba_ref,wr,state_dut[182],state_dut[185],state_dut[47],state_dut[210],state_dut[296],aec_dut,aec_ref);
        if(retain_edges>0)retain_edges--;trace_y=y;last_lp=lp;
    end
    always @(negedge clk)begin
        if(!reset)begin
            tick++;phase=(phase+1)%32;if(phase==0)cycle++;
            if(phase==15)begin if(y<last_y)frames++;last_y=y;end
            // Randomness depends only on the clock, never the observed result.
            random_state={random_state[30:0],random_state[31]^random_state[21]^random_state[1]^random_state[0]};
            lp=!(frames>=1&&y==100&&phase>=6&&phase<=9);
            if(frames>=1&&y>=40&&y<240)begin
                // At most one generic update per window; directed adjacent
                // pulses below cover the event-preserving collapse cases.
                if(y>=120&&phase==random_state[4:0]&&random_state[10:8]==0)begin
                    case(random_state[14:11])
                    0:if(y>=50)enqueue('h11,{1'b0,random_state[19:18],1'b1,1'b1,random_state[22:20]});
                    1:enqueue('h15,random_state[23:16]);
                    2:enqueue('h17,random_state[23:16]);
                    3:enqueue('h18,random_state[23:16]);
                    4,5,6,7:enqueue({2'b00,random_state[17:15],1'b1},random_state[31:24]);
                    8:enqueue('h12,random_state[23:16]);
                    9:enqueue('h19,random_state[23:16]&15);
                    10:enqueue('h1a,random_state[23:16]&15);
                    default:enqueue('h20,random_state[23:16]);
                    endcase
                end
                if(y==48&&phase==4)enqueue('h11,'h1b);
                if(y==48&&phase==5)enqueue('h11,'h0b);
                if(y==50&&phase==4)enqueue('h11,'h1b);
                if(y==60&&phase==4)enqueue('h12,60);
                if(y==60&&phase==5)enqueue('h12,61);
                if(y==62&&phase==4)enqueue('h19,'hf1);
                if(y==62&&phase==5)enqueue('h19,'hf0);
                if(y==70&&phase==10)enqueue('h11,cycle[0]?'h3b:'h1b);
                if(y==100&&state_dut[3:0]==8&&phase==4)enqueue('h01,101);
                if(y==100&&state_dut[3:0]==8&&phase==8)enqueue('h15,'hff);
                if(y==100&&state_dut[3:0]==8&&phase==13)enqueue('h01,100);
                if(y==60&&state_dut[3:0]==8&&phase==4)enqueue('h01,60);
                if(y==60&&state_dut[3:0]==8&&phase==8)enqueue('h15,'hff);
                if(y==60&&state_dut[3:0]==10&&phase==4)enqueue('h01,61);
                if(y==60&&state_dut[3:0]==12&&state_dut[231:229]==0&&phase==18)enqueue('h01,60);
            end
        end
    end
    initial begin
        $readmemh("case.hex",config_bytes);ntsc=config_bytes[0];replay=config_bytes[1];mutant=config_bytes[2];
        if(replay)$readmemh("mapped.hex",mapped);
        trace_file=$fopen("state.csv","w");event_file=$fopen("events.csv","w");
        if(!trace_file||!event_file)$fatal(1,"equivalence trace open failed");
        $fdisplay(trace_file,"tick,phase,y,reset,lp,dut,reference,address_dut,address_reference,ba_reu,ba_reference,write,irst,ilp,den,done,last_line,aec_dut,aec_reference");
        $fdisplay(event_file,"tick,phase,y,address,data");
        repeat(64)@(negedge clk);#1;reset=0;
        // Initialize all Y comparators and the readable display fields.
        for(integer a=0;a<8;a++)begin repeat(8)@(negedge clk);#1;enqueue(2*a+1,80+8*a);end
        repeat(8)@(negedge clk);#1;enqueue('h11,'h0b);
        repeat(8)@(negedge clk);#1;enqueue('h15,'hff);
        repeat(8)@(negedge clk);#1;enqueue('h18,'h14);
        wait(frames>=3);repeat(64)@(negedge clk);
        $fclose(trace_file);$fclose(event_file);
        if(events<100||cycle<30000)$fatal(1,"equivalence coverage");
        $display("PASS VIC trace ntsc=%0d replay=%0d mutant=%0d events=%0d cycles=%0d",ntsc,replay,mutant,events,cycle);$finish;
    end
    initial begin repeat(4*312*65*32)@(posedge clk);$fatal(1,"VIC equivalence timeout");end
endmodule

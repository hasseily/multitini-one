// Original software ABI fixture. SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_helper;
    parameter integer MODESEL=0,SCENARIO=0,NEGATIVE=0;
    reg clk=0,reset=1; always #5 clk=!clk;
    reg [1:0] phase=0; always @(posedge clk)phase<=phase+1'b1;
    wire rw,as_n,uds_n,lds_n;
    wire [23:1] address;wire [15:0] data_out;
    reg [15:0] ram[0:524287];wire [15:0] mailbox[0:17];
    reg [15:0] host_request=(SCENARIO==20 || SCENARIO==23) ? (4|MODESEL):0;
    wire read_selected;wire [15:0] native_read_data;
    multitini_amiga_display_mailbox mailbox_i(.service_clk(clk),.native_clk(clk),
        .reset(reset),.guest_reset(1'b0),.request(host_request),.address(address),
        .as_n(as_n),.rw(rw),.uds_n(uds_n),.lds_n(lds_n),.dtack_n(as_n),
        .write_data(data_out),.read_selected(read_selected),.read_data(native_read_data),
        .arm_address(5'd0),.arm_data());
    assign mailbox[0]=16'h4d54;assign mailbox[1]=1;assign mailbox[2]=host_request;
    genvar g;generate for(g=3;g<18;g=g+1)begin
        assign mailbox[g]=mailbox_i.words[g];
    end endgenerate
    wire [23:0] byte_address={address,1'b0};
    wire mmio=byte_address>=24'hde0f00 && byte_address<24'hde0f24;
    wire gui_vector=byte_address==24'h3000-474 || byte_address==24'h3000-510 ||
        byte_address==24'h3000-516 || byte_address==24'h3000-522 || byte_address==24'h3000-528 ||
        byte_address==24'h4000-708 || byte_address==24'h4000-756 ||
        byte_address==24'h4000-792 || byte_address==24'h4000-798;
    wire [15:0] data_in=mmio ? (SCENARIO==6 ? 16'd0:native_read_data) :
        byte_address<24'h100000 ? ram[address] : 16'hffff;
    fx68k cpu(.clk(clk),.enPhi1(phase==0),.enPhi2(phase==2),
        .extReset(reset),.pwrUp(reset),.HALTn(1'b1),.eRWn(rw),.ASn(as_n),
        .LDSn(lds_n),.UDSn(uds_n),.DTACKn(as_n),.VPAn(1'b1),.BERRn(1'b1),
        .BRn(1'b1),.BGACKn(1'b1),.IPL0n(1'b1),.IPL1n(1'b1),.IPL2n(1'b1),
        .iEdb(data_in),.oEdb(data_out),.eab(address));
    integer pointer,k,saved_polls,saved_heartbeat,acknowledgements=0;
    reg [15:0] snapshot[0:17];reg write_seen=0;
    always @(posedge clk)begin
        if(!reset && !as_n && rw && gui_vector && ram[16'h5828>>1]!=0)
            $fatal(1,"Intuition/graphics called while preferences child has no terminal receipt");
        if(as_n)write_seen<=0;
        if(!reset && !as_n && !rw && (!uds_n || !lds_n) && !write_seen)begin
            write_seen<=1;
            if(mmio)begin
                // Match the ARM policy: a new request is published only once
                // this incarnation has demonstrably advanced its heartbeat.
                if(byte_address==24'hde0f0a)begin
                    if(SCENARIO!=20 && SCENARIO!=23 && data_out==2)host_request<=4|(SCENARIO==38 ? 2:MODESEL);
                    if(SCENARIO==20 && data_out==5)begin
                        if(ram[16'h5808>>1]!=0 || acknowledgements!=0)
                            $fatal(1,"Replayed request from before helper startup");
                        host_request<=8|MODESEL;
                    end
                    if(SCENARIO==23 && data_out==5)begin
                        if(ram[16'h5808>>1]!=0 || acknowledgements!=0)
                            $fatal(1,"Replayed acknowledged request from previous helper");
                        for(k=3;k<18;k=k+1)if(k!=5 && snapshot[k]!=mailbox[k])
                            $fatal(1,"Old ACK metadata/flags rewritten index%0d",k);
                        if(mailbox[15]!=16'h002f)$fatal(1,"Old capability-free flags changed");
                        host_request<=8|MODESEL;
                    end
                end
                if(byte_address<24'hde0f06)$fatal(1,"Guest wrote read-only register");
                if(byte_address==24'hde0f06)begin
                    if(ram[16'h5832>>1]!=ram[16'h5838>>1] || ram[16'h5836>>1]!=0 ||
                       ram[16'h583a>>1]!=0 || ram[16'h583c>>1]!=0 || ram[16'h584c>>1]!=0)
                        $fatal(1,"Owned output leaked/doubleclosed or caller stream transferred");
                    if(data_out!=mailbox[2])$fatal(1,"Stale acknowledgement %h request%h",data_out,mailbox[2]);
                    if(mailbox[4]==0 && ((mailbox[15]&16'h6f)!=16'h6f ||
                       mailbox[8]!=((data_out&3)==3 ? 2:4)))
                        $fatal(1,"False successful ACK flags%h depth%h",mailbox[15],mailbox[8]);
                    acknowledgements<=acknowledgements+1;
                end
            end else if(byte_address<24'h100000)begin
                if(SCENARIO==30 && byte_address==24'h582a && data_out==1 && ram[16'h5808>>1]==1)
                    host_request<=8|MODESEL; // Supersede inside the fresh GUI query.
                if(byte_address==24'h5830 && data_out!=host_request)
                    $fatal(1,"Superseded GUI query launched an old SAVE");
                // The library stubs record each call. A list lock must never
                // cover a screen call or cooperative DOS wait, and a missing
                // or private Workbench entry must never trigger its auto-open.
                if(byte_address==24'h5822)begin
                    if(ram[16'h5820>>1]!=0)$fatal(1,"LockPubScreen called while public list held");
                    if({ram[16'h6440>>1],ram[16'h6442>>1]}!=32'h6460 || ram[16'h6472>>1]!=0)
                        $fatal(1,"LockPubScreen called with absent/private Workbench");
                end
                if(byte_address==24'h5802 && ram[16'h5820>>1]!=0)
                    $fatal(1,"DOS Delay called while public list held");
                if((SCENARIO==3 || SCENARIO==13) && byte_address==24'h5802 && data_out==(SCENARIO==3 ? 2:5))host_request<=8;
                if(!uds_n)ram[address][15:8]<=data_out[15:8];
                if(!lds_n)ram[address][7:0]<=data_out[7:0];
            end
        end
        if(!reset && ram[16'h5802>>1]>32)$fatal(1,"Helper did not finish polls%0d",ram[16'h5802>>1]);
    end
    initial begin
        if(NEGATIVE)$readmemh("legacy-memory.hex",ram);
        else $readmemh("memory.hex",ram);
        ram[16'h580c>>1]=SCENARIO;
        repeat(30)@(posedge clk);@(negedge clk);reset=0;
        if(SCENARIO==23)begin
            // Model a retained version 1 ACK from the old three-mode daemon.
            // The new daemon must not rewrite even its capability flags until
            // ARM issues a fresh request after observing advancing heartbeat.
            for(k=3;k<18;k=k+1)mailbox_i.words[k]=16'h1000+k;
            mailbox_i.words[3]=host_request;mailbox_i.words[4]=4;
            mailbox_i.words[5]=0;mailbox_i.words[15]=16'h002f;
            #1;
            for(k=3;k<18;k=k+1)snapshot[k]=mailbox[k];
        end
        if(NEGATIVE)begin
            wait(ram[16'h580a>>1]==16'habcd);repeat(20)@(posedge clk);
            if(ram[16'h583e>>1]!=2 || ram[16'h583c>>1]!=1 || ram[16'h583a>>1]!=16'hdead ||
               {ram[16'h5842>>1],ram[16'h5844>>1]}!=0 ||
               {ram[16'h5846>>1],ram[16'h5848>>1]}!=32'hffffffff)
                $fatal(1,"Legacy async inherited-stream negative control did not fail second launch");
            $display("LEGACY_SHARED_STREAM_FAILURE_REPRODUCED");$finish;
        end else if(SCENARIO==24)begin
            wait(ram[16'h5802>>1]>=6);repeat(20)@(posedge clk);
            if(acknowledgements!=0 || ram[16'h5808>>1]!=0 || ram[16'h5822>>1]!=0)
                $fatal(1,"Absent Workbench consumed request/opened screen/launched preferences");
            if(mailbox[5]<6 || mailbox[15]!=16'h0041 || mailbox[6]!=0 || mailbox[7]!=0)
                $fatal(1,"Absent Workbench did not retain healthy heartbeat/alive state");
            if(ram[16'h5824>>1]!=ram[16'h5826>>1] || ram[16'h5824>>1]<6)
                $fatal(1,"Unbalanced public list locks while Workbench absent");
        end else if(SCENARIO==5 || SCENARIO==6 || SCENARIO==9 || SCENARIO==11)begin
            wait(ram[16'h580a>>1]==16'habcd);
            if(acknowledgements!=0)$fatal(1,"Unavailable helper acknowledged");
            if(SCENARIO==11 && ram[16'h581c>>1]!=2)$fatal(1,"Partial library cleanup failed");
        end else begin
            wait(acknowledgements==1);repeat(10)@(posedge clk);
            if(SCENARIO==38)begin
                if(mailbox[4]!=0 || mailbox[6]!=724 || mailbox[7]!=566 ||
                   ram[16'h5850>>1]!=93 || ram[16'h5852>>1]!=29)
                    $fatal(1,"Roundtrip first maximum request did not apply shifted normalized bounds");
                pointer={ram[16'h5810>>1],ram[16'h5812>>1]};
                if(ram[(pointer+62)>>1]!=0 || ram[(pointer+64)>>1]!=0 ||
                   ram[(pointer+66)>>1]!=723 || ram[(pointer+68)>>1]!=282)
                    $fatal(1,"Roundtrip maximum rectangle was not normalized");
                @(negedge clk);host_request=8|MODESEL;
                wait(acknowledgements==2);repeat(10)@(posedge clk);
                if(ram[16'h5850>>1]!=129 || ram[16'h5852>>1]!=44)
                    $fatal(1,"Maximum-to-standard did not restore immutable default monitor position");
            end
            if(SCENARIO==0 || SCENARIO==3 || SCENARIO==13 || SCENARIO==15 || SCENARIO==16 || SCENARIO==17 || SCENARIO==19 || SCENARIO==20 || SCENARIO==23 || SCENARIO==25 || SCENARIO==26 || SCENARIO==27 || SCENARIO==28 || SCENARIO==29 || SCENARIO==30 || SCENARIO==37 || SCENARIO==38 || SCENARIO==41)begin
                if(mailbox[4]!=0)$fatal(1,"Expected success got%0d",mailbox[4]);
                if(ram[16'h5808>>1]!=(SCENARIO==3 || SCENARIO==30 ? 3:SCENARIO==13 || SCENARIO==38 ? 4:2))$fatal(1,"Wrong child count");
                if(mailbox[5]<6)$fatal(1,"Heartbeat failed during pending child");
                if((mailbox[3]&3)==3 && (mailbox[6]!=1280 || mailbox[7]!=512 ||
                   mailbox[8]!=2 || {mailbox[10],mailbox[9]}!=32'h29024))
                    $fatal(1,"Super-HiRes metadata does not match actual screen");
                pointer={ram[16'h5810>>1],ram[16'h5812>>1]};
                if({ram[(pointer+50)>>1],ram[(pointer+52)>>1]}!=32'h29000 ||
                   ram[(pointer+62)>>1]!=0 || ram[(pointer+64)>>1]!=0 ||
                   {ram[16'h5814>>1],ram[16'h5816>>1]}!=70)
                    $fatal(1,"OSCN full HiRes mode ID/zero-based rectangle/length incorrect");
                if((mailbox[3]&3)==2)begin
                    if(ram[(pointer+54)>>1]!=93 || ram[(pointer+56)>>1]!=29 ||
                       ram[(pointer+58)>>1]!=724 || ram[(pointer+60)>>1]!=283 ||
                       ram[(pointer+66)>>1]!=723 || ram[(pointer+68)>>1]!=282)
                        $fatal(1,"Maximum OSCN signed conversion/normalization incorrect");
                    if(mailbox[11]!=0 || mailbox[12]!=0 || mailbox[13]!=723 || mailbox[14]!=565)
                        $fatal(1,"Lace maximum metadata incorrect");
                end else begin
                    if(ram[(pointer+54)>>1]!=129 || ram[(pointer+56)>>1]!=44 ||
                       ram[(pointer+58)>>1]!=640 || ram[(pointer+60)>>1]!=256 ||
                       ram[(pointer+66)>>1]!=639 || ram[(pointer+68)>>1]!=255)
                        $fatal(1,"Standard OSCN did not restore nominal bounds/default view position");
                end
                if(SCENARIO==37 && ram[16'h584a>>1]<4)$fatal(1,"GET tag was not consumed/rearmed across repeated queries");
            end
            if((SCENARIO==1 || SCENARIO==12 || SCENARIO==14 || SCENARIO==18) && mailbox[4]!=3)$fatal(1,"Preferences failure lost");
            if((SCENARIO==31 || SCENARIO==32 || SCENARIO==33) && mailbox[4]!=3)
                $fatal(1,"Owned output Open/Close failure accepted");
            if(SCENARIO==31 && (ram[16'h5840>>1]!=1 || ram[16'h5832>>1]!=0 || ram[16'h5838>>1]!=0 || ram[16'h5808>>1]!=0))
                $fatal(1,"Failed NIL Open closed or launched an unowned stream");
            if(SCENARIO==12 && (ram[16'h5832>>1]!=1 || ram[16'h5838>>1]!=1))
                $fatal(1,"Execute FALSE did not close owned output exactly once");
            if(SCENARIO==32 && (ram[16'h5832>>1]!=1 || ram[16'h5838>>1]!=1 || ram[16'h5808>>1]!=1))
                $fatal(1,"Close failure retried/doubleclosed or changed execution count");
            if(SCENARIO==33 && (ram[16'h5840>>1]!=2 || ram[16'h5832>>1]!=1 || ram[16'h5838>>1]!=1 || ram[16'h5808>>1]!=1))
                $fatal(1,"Second-pass NIL Open failure did not preserve first-pass ownership");
            if((SCENARIO==2 || SCENARIO==21 || SCENARIO==22 || SCENARIO==36 || SCENARIO==40 || SCENARIO==42 || SCENARIO==43) && mailbox[4]!=4)
                $fatal(1,"Invalid viewport/depth/ModeID accepted");
            if((SCENARIO==2 || SCENARIO==21 || SCENARIO==22 || SCENARIO==36 || SCENARIO==40 || SCENARIO==42 || SCENARIO==43) && ram[16'h582a>>1]!=10)
                $fatal(1,"Permanent mismatch did not use exactly ten fresh verification polls");
            if((SCENARIO==36 || SCENARIO==40 || SCENARIO==42 || SCENARIO==43) && (mailbox[15]&16'h20)!=0)
                $fatal(1,"Unknown/partial physical clip advertised complete visible bitmap");
            if(SCENARIO==29 && ram[16'h582a>>1]!=4)
                $fatal(1,"SAVE screen publication was not freshly reverified after settling");
            if((SCENARIO==4 || SCENARIO==8 || SCENARIO==34 || SCENARIO==35 || SCENARIO==39) && mailbox[4]!=2)$fatal(1,"Unsupported mode accepted");
            if((SCENARIO==34 || SCENARIO==35 || SCENARIO==39) && ram[16'h5808>>1]!=0)
                $fatal(1,"Invalid monitor record/origin launched preferences");
            if(SCENARIO==7 && MODESEL==2 && mailbox[4]!=3)$fatal(1,"Short OSCN write accepted");
        end
        if(SCENARIO==17 || SCENARIO==18)begin
            for(k=3;k<18;k=k+1)snapshot[k]=mailbox[k];
            saved_polls=ram[16'h5802>>1];saved_heartbeat=mailbox[5];
            // Change the fake live Workbench after ACK. A persistent idle
            // query would rewrite dimensions/depth/mode/flags and must fail.
            ram[(24'h6000+12)>>1]=320;ram[(24'h6000+14)>>1]=200;
            ram[(24'h6000+68)>>1]=320;ram[(24'h6000+70)>>1]=200;
            ram[24'h6204>>1]=16'h0002;ram[24'h6300>>1]=0;ram[24'h6302>>1]=0;
            wait(ram[16'h5802>>1]>=saved_polls+4);repeat(20)@(posedge clk);
            for(k=3;k<18;k=k+1)if(k!=5 && snapshot[k]!=mailbox[k])
                $fatal(1,"ACK snapshot mutated index%0d old%h new%h",k,snapshot[k],mailbox[k]);
            if(mailbox[5]<saved_heartbeat+3)$fatal(1,"Frozen snapshot stopped heartbeat");
            if(acknowledgements!=1)$fatal(1,"Idle snapshot rewrote ACK");
        end
        $display("GUEST_HELPER_PASS mode%0d scenario%0d",MODESEL,SCENARIO);$finish;
    end
    initial begin #100000000;$fatal(1,"CPU/helper timeout bus%h polls%0d",byte_address,ram[16'h5802>>1]);end
endmodule

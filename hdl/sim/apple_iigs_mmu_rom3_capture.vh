// SPDX-License-Identifier: GPL-3.0-only
// Included in the production-state bench. Expected observations are the
// physical ROM03 capture, not values calculated by an emulator or HDL oracle.
// Backing-memory marker effects are classified from production bus selects;
// this fixture does not execute the native probe or model memory timing.
reg [7:0] capture_expected[0:45999];
integer capture_cursor=0;
integer capture_addresses[14]='{'h03ff,'h0400,'h07ff,'h0800,'h0bff,'h0c00,
    'h1fff,'h2000,'h3fff,'h4000,'h5fff,'h6000,'h9fff,'ha000};
integer capture_aux_addresses[8]='{'h0100,'h01ff,'h0200,'h03ff,'h0400,'h07ff,'h2000,'h4000};
integer capture_sources[10]='{0,1,2,3,14,15,126,127,0,2};
integer capture_aliases[4]='{0,1,'he0,'he1};
integer capture_registers[5]='{'hc029,'hc035,'hc036,'hc068,'hc02d};

task capture_bus(input [23:0] a,input [7:0] v,input bit rd);
    @(negedge clk);address=a;data=v;read_cycle=rd;commit=1;
    @(posedge clk);#1;
    @(negedge clk);commit=0;transactions++;
endtask
task capture_reg(input [15:0] a,input [7:0] v);
    capture_bus({8'he0,a},v,0);
endtask
task capture_base;
    capture_reg(16'hc035,8'h7f);
    capture_reg(16'hc036,8'h80);
    capture_reg(16'hc068,8'h08);
    capture_reg(16'hc000,0);
    capture_reg(16'hc056,0);
    capture_reg(16'hc029,1);
endtask
task capture_address(input [7:0] b,input [15:0] a,input bit rd);
    @(negedge clk);address={b,a};read_cycle=rd;commit=0;#1;
endtask
function automatic [7:0] capture_destinations;
    reg [7:0] v;
    v=0;
    if(dut.fastram_ce)v=v | (8'h01 << int'(dut.addr_bus[16]));
    if(dut.slowram_ce)v=v | (8'h04 << int'(dut.addr_bus[16]));
    return v;
endfunction
task capture_compare(input [7:0] actual,input string family);
    group_name=family;
    expect_byte(actual,capture_expected[capture_cursor],$sformatf("ROM03 observation %0d",capture_cursor));
    capture_cursor++;
endtask
task capture_switch(input [7:0] b,input integer op);
    capture_bus({b,16'hc080+16'(op&15)},0,op<16);
endtask
task capture_rom3;
    reg [7:0] result,read_source,bank_id;
    string fixture;
    if(!$value$plusargs("ROM3_CAPTURE=%s",fixture))$fatal(1,"Missing physical ROM03 fixture");
    $readmemh(fixture,capture_expected);
    rom1=0;
    // Exact native-probe profile/case order: 280*128 shadow observations.
    for(integer source=0;source<10;source++)for(integer a=0;a<14;a++)
    for(integer all_banks=0;all_banks<2;all_banks++)for(integer c=0;c<128;c++)begin
        capture_base();
        capture_reg(16'hc068,source>=8 ? 8'h18:8'h08);
        capture_reg(16'hc035,8'(c));
        capture_reg(16'hc036,all_banks!=0 ? 8'h90:8'h80);
        capture_address(8'(capture_sources[source]),16'(capture_addresses[a]),0);
        capture_compare(capture_destinations(),"captured shadow");
    end
    // Read source and write destinations under all 128 aux/video settings.
    for(integer b=0;b<4;b++)for(integer a=0;a<8;a++)for(integer c=0;c<128;c++)begin
        bank_id=b==0 ? 0:b==1 ? 2:b==2 ? 8'he0:8'he1;
        capture_base();
        capture_reg(16'hc000+16'((c>>4)&1),0);
        capture_reg(16'hc056+16'((c>>5)&1),0);
        capture_reg(16'hc068,8'h08 | (c[0] ? 8'h20:0) | (c[1] ? 8'h10:0) |
            (c[2] ? 8'h80:0) | (c[3] ? 8'h40:0));
        capture_reg(16'hc029,8'((c>>6)&1));
        capture_address(bank_id,16'(capture_aux_addresses[a]),1);
        read_source=(dut.slowram_ce ? 8'd2:8'd0)+{7'd0,dut.addr_bus[16]};
        capture_address(bank_id,16'(capture_aux_addresses[a]),0);
        capture_compare(capture_destinations() | (read_source<<4),"captured aux");
    end
    for(integer b=0;b<4;b++)for(integer first=0;first<32;first++)for(integer second=0;second<32;second++)begin
        capture_base();capture_reg(16'hc035,0);
        capture_switch(8'he0,2);
        capture_switch(8'(capture_aliases[b]),first);
        capture_switch(8'(capture_aliases[b]),second);
        result={5'd0,dut.RDROM,dut.LCRAM2,1'b0};
        capture_address(0,16'hd800,0);
        capture_compare(result | {7'd0,dut.fastram_ce},"captured LC pairs");
    end
    for(integer b=0;b<4;b++)for(integer alt=0;alt<2;alt++)for(integer inhibited=0;inhibited<2;inhibited++)
    for(integer op=0;op<32;op++)begin
        capture_base();capture_switch(8'he0,2);
        capture_reg(16'hc068,alt!=0 ? 8'h88:8'h08);
        capture_reg(16'hc035,inhibited!=0 ? 8'h40:0);
        capture_switch(8'he0,op);capture_switch(8'he0,op);
        capture_address(8'(capture_aliases[b]),16'hd800,1);
        result=dut.rom2_ce ? 8'd2:(dut.addr_bus[15:12]==4'hc ? 8'd1:8'd0);
        capture_address(8'(capture_aliases[b]),16'he800,1);
        if(dut.rom2_ce)result=result | 8'h04;
        capture_address(8'(capture_aliases[b]),16'hd800,0);
        if(dut.fastram_ce || dut.slowram_ce)result=result | 8'h10;
        capture_address(8'(capture_aliases[b]),16'he800,0);
        if(dut.fastram_ce || dut.slowram_ce)result=result | 8'h20;
        capture_compare(result,"captured LC windows");
    end
    for(integer r=0;r<5;r++)for(integer half=0;half<2;half++)for(integer c=0;c<128;c++)begin
        capture_base();capture_reg(16'(capture_registers[r]),8'(c|(half<<7)));
        capture_address(8'he0,16'(capture_registers[r]),1);
        capture_compare(dut.io_dout,"captured registers");
    end
    if(capture_cursor!=45824)$fatal(1,"Incomplete physical fixture");
    $display("PASS: physical ROM03 capture observations=%0d",capture_cursor);
    capture_supplemental();
endtask

task capture_supplemental;
    reg [7:0] result,bank_id;
    integer missing[4]='{'h80,'h81,'h82,'hdf};
    integer io_banks[8]='{0,1,2,3,14,15,126,127};
    for(integer b=0;b<4;b++)for(integer a=0;a<8;a++)begin
        capture_base();capture_address(8'(missing[b]),16'(a),1);
        capture_compare(dut.cpu_din,"captured absent memory");
        if(dut.fastram_ce || dut.slowram_ce || dut.rom_ce)$fatal(1,"Absent bank selected memory");
    end
    for(integer b=0;b<2;b++)for(integer alt=0;alt<2;alt++)begin
        capture_base();capture_reg(16'hc068,alt!=0 ? 8'h88:8'h08);
        capture_reg(16'hc035,8'h7f);
        for(integer lc=0;lc<2;lc++)begin
            capture_switch(8'he0,lc!=0 ? 11:3);
            capture_address(8'(b),16'hc800,1);
            capture_compare(dut.addr_bus[15:12]==4'hd ? 8'h19:8'h2b,"captured flat C800");
            capture_address(8'(b),16'hd800,1);
            capture_compare(dut.addr_bus[15:12]==4'hd ? 8'h19:8'h2b,"captured flat D800");
        end
    end
    for(integer b=0;b<8;b++)for(integer all_banks=0;all_banks<2;all_banks++)begin
        capture_base();capture_reg(16'hc035,0);
        capture_reg(16'hc036,all_banks!=0 ? 8'h90:8'h80);
        capture_address(8'(io_banks[b]),16'hc010,0);
        result=dut.fastram_ce ? 8'h12:0;
        capture_bus({8'(io_banks[b]),16'hc005},8'h12,0);
        capture_address(8'he0,16'hc068,1);
        capture_compare(result,"captured extended C010");
        capture_compare(dut.io_dout,"captured extended C005");
    end
    for(integer c=0;c<16;c++)begin
        capture_base();capture_reg(16'hc035,c[0] ? 8'h3f:8'h1f);
        capture_reg(16'hc036,c[1] ? 8'hd0:8'hc0);
        capture_address(8'(c>>2),16'h0800,0);
        result=capture_destinations();
        capture_compare(result[2] ? 8'h96:0,"captured text2 E0");
        capture_compare(result[3] ? 8'h96:0,"captured text2 E1");
    end
    for(integer c=0;c<64;c++)begin
        capture_base();capture_reg(16'hc035,0);
        capture_bus(24'he0cfff,0,1);
        capture_reg(c[1] ? 16'hc00b:16'hc00a,0);
        capture_reg(16'hc02d,c[2] ? 8'h08:0);
        capture_reg(16'hc068,8'h08|8'(c&1));
        case(c>>3)
            1:capture_bus(24'he0c300,0,1);
            2:capture_bus(24'he0c300,0,0);
            3:capture_bus(24'he0c100,0,1);
            4:capture_bus(24'he0c700,0,1);
            5:begin capture_bus(24'he0c300,0,1);capture_bus(24'he0c100,0,1);end
            6:begin capture_bus(24'he0c300,0,1);capture_bus(24'he0cfff,0,1);end
            7:begin capture_bus(24'he0c300,0,1);capture_bus(24'he0cfff,0,0);end
            default:;
        endcase
        capture_address(8'he0,16'hc800,1);result={7'd0,dut.rom2_ce};
        capture_address(8'he0,16'hcfff,1);result=result | (dut.rom2_ce ? 8'd2:0);
        capture_bus(24'he0cfff,0,1);
        capture_address(8'he0,16'hc800,1);result=result | (dut.rom2_ce ? 8'd4:0);
        capture_compare(result,"captured C800 ownership");
    end
    if(capture_cursor!=46000)$fatal(1,"Incomplete supplemental fixture");
    $display("PASS: physical ROM03 supplemental observations=176");
endtask

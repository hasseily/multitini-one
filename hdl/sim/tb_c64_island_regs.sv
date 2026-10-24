// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_island_regs;
    reg clk_native=0,reset=1,config_valid=1;
    always #5 clk_native=~clk_native;
    reg [31:0] config_word=32'h00000551;
    reg legacy_turbo_request=0,native_tick=0,cart_present=0,loader_active=0,pause_request=0,guard_active=1;
    reg [1:0] guard_cause=0;
    reg native_wr=0,fio_wr=0;
    reg [9:0] native_addr=0,fio_addr=0,native_rd_addr=0,fast_rd_addr=0;
    reg [7:0] native_data=0,fio_data=0;
    reg active_turbo=0,active_c64u_top=0;
    reg [3:0] active_index=0;
    reg diag_deadline_miss=0,diag_ioonly_done=0,diag_nmi_used=0;
    wire native_rd_override,fast_rd_override,policy_turbo,policy_blocked,policy_badline,policy_c64u_top;
    wire [7:0] native_rd_data,fast_rd_data,live_d031;
    wire [3:0] policy_index;
    wire guard_request,guard_static_release;
    wire [1:0] guard_holdoff;
    wire [31:0] status;
    multitini_c64_turbo_regs #(.CORE_CLOCK_HZ(96),.RESET_WINDOW_CYCLES(6)) dut(.*);
    integer checks=0,m,s,a;
    reg [7:0] before_mirror;
    task automatic step;@(posedge clk_native);#0.01;endtask
    task automatic check(input bit ok,input string why);
        checks++;if(!ok)$fatal(1,"REGS FAIL %s config=%x live=%x status=%x",why,config_word,live_d031,status);
    endtask
    task automatic boot(input integer mode);
        @(negedge clk_native);reset=1;native_tick=0;config_word=32'h0000054f|(mode<<4);
        guard_active=1;guard_cause=0;cart_present=0;loader_active=0;pause_request=0;
        diag_deadline_miss=0;diag_ioonly_done=0;diag_nmi_used=0;
        native_wr=0;fio_wr=0;repeat(2)step();@(negedge clk_native);reset=0;step();
        check(live_d031==0 && policy_blocked,"first word is not apply");
    endtask
    task automatic write_reg(input bit fast,input [9:0] address,input [7:0] data);
        @(negedge clk_native);native_wr=!fast;fio_wr=fast;native_addr=address;fio_addr=address;native_data=data;fio_data=data;
        step();@(negedge clk_native);native_wr=0;fio_wr=0;
    endtask
    task automatic end_window;
        @(negedge clk_native);native_tick=1;repeat(6)step();@(negedge clk_native);native_tick=0;
    endtask
    task automatic apply(input integer mode,input [3:0] index);
        @(negedge clk_native);config_word[5:4]=mode;config_word[3:0]=index;config_word[15:12]=config_word[15:12]+1'b1;step();
    endtask
    initial begin
        for(m=0;m<4;m++)begin
            boot(m);end_window();
            check(live_d031==(m==2?8'h0f:8'h00),"reset window default");
            check(policy_turbo==(m==1 || m==2),"default effective mode");
            for(s=0;s<2;s++)begin
                before_mirror=live_d031;
                write_reg(s,10'h071,8'h83);check(live_d031==before_mirror,"D071 is not D031 mirror");
                write_reg(s,10'h031,8'hf3);
                check(live_d031==(m>=2?8'h83:8'h00),"D031 masked/liveness");
                check(status[22]==(m<2),"ignored register hint");
                write_reg(s,10'h030,8'h01);
                check(live_d031==(m==3?8'h0f:m==2?8'h83:8'h00),"D030 mode rule");
                check(status[23]==(m!=3),"ignored C128 hint");
                write_reg(s,10'h07a,0);check(live_d031==0,"D07A off");
                write_reg(s,10'h079,0);check(live_d031==(m>=2?8'h0f:0),"D079 preference");
                write_reg(s,10'h07a,0);write_reg(s,10'h07b,0);check(live_d031==(m>=2?8'h0f:0),"D07B preference");
            end
        end
        boot(2);write_reg(0,10'h031,8'h84);check(policy_blocked && live_d031==8'h84,"window latches write");
        @(negedge clk_native);pause_request=1;native_tick=1;repeat(10)step();check(policy_blocked,"pause stops reset window");
        @(negedge clk_native);pause_request=0;native_tick=0;end_window();
        check(live_d031==8'h84 && policy_turbo && !policy_badline,"window preserves software override");
        apply(3,5);check(live_d031==0 && status[23:21]==0,"enter TurboEnable clears live and hints");
        apply(3,7);check(live_d031==0,"apply preserves software off");
        write_reg(1,10'h030,1);check(live_d031==7,"TurboEnable on preference");
        apply(3,9);check(live_d031==9,"apply enabled updates preference");
        apply(2,4);check(live_d031==4,"Registers apply always preference");
        @(negedge clk_native);config_word[7]=1;config_word[11]=1;config_word[17:16]=3;step();
        check(live_d031==4 && policy_c64u_top && guard_static_release && guard_holdoff==3,"debug knobs no apply");
        for(a=0;a<1024;a++)begin
            native_rd_addr=a;fast_rd_addr=a;#0.01;
            check(native_rd_override==(a==48 || a==49) && fast_rd_override==native_rd_override,"exact read decode");
            if(a==49)check(native_rd_data==4 && fast_rd_data==4,"live readback");
        end
        @(negedge clk_native);cart_present=1;step();check(policy_blocked && !policy_turbo && live_d031==4 && status[16],"cart forces exact retains live");
        @(negedge clk_native);cart_present=0;loader_active=1;step();check(!policy_turbo && status[18],"loader forces exact");
        @(negedge clk_native);loader_active=0;guard_active=0;guard_cause=2;step();check(!policy_turbo && status[17] && status[7:6]==2,"guard blocks cause");
        @(negedge clk_native);config_word[8]=0;step();check(policy_turbo && !status[17] && status[7:6]==0,"guard off");
        diag_deadline_miss=1;diag_ioonly_done=1;diag_nmi_used=1;repeat(3)step();check(status[26:24]==0,"diagnostics disarmed");
        @(negedge clk_native);config_word[19]=1;step();check(status[26:24]==0,"arming clears");step();check(status[26:24]==7,"armed events captured");
        @(negedge clk_native);config_word[19]=0;step();check(status[26:24]==0,"disarm clears");
        boot(1);config_word[10]=0;end_window();check(!policy_turbo,"legacy CONTROL off");
        legacy_turbo_request=1;#0.01;check(policy_turbo,"legacy CONTROL on");
        $display("REGS PASS checks=%0d",checks);$finish;
    end
endmodule

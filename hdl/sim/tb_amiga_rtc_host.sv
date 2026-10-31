// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_amiga_rtc_host;
    reg clk=0,control_clk=0,reset=1,control_resetn=0,write=0;
    always #5 clk=~clk;
    always #7 control_clk=~control_clk;
    reg native_running=1,native_enable=1;
    always @(negedge clk)native_enable<=native_running;
    wire chip_tick=!reset && native_enable;
    wire native_clk;
    assign #0.1 native_clk=clk && native_enable;
    reg [31:0] command=0;
    wire [31:0] status;
    wire io_fpga,io_uio,io_strobe,ide_read,ide_write;
    wire [15:0] io_din,ide_wdata;
    wire [4:0] ide_address;
    wire [64:0] native_rtc;
    multitini_amiga_host dut(.control_clk(control_clk),.control_resetn(control_resetn),
        .write(write),.command(command),.status(status),.clk(clk),.reset(reset),.chip_tick(chip_tick),
        .io_fpga(io_fpga),.io_uio(io_uio),.io_strobe(io_strobe),.io_din(io_din),
        .io_wait(1'b0),.io_dout(16'd0),.ide_req(6'd0),.ide_address(ide_address),
        .ide_read(ide_read),.ide_write(ide_write),.ide_wdata(ide_wdata),.ide_rdata(16'd0),
        .native_rtc(native_rtc));

    localparam [63:0] VALUE_A=64'h1122334455667788,VALUE_B=64'h8877665544332211,
                      VALUE_C=64'h0123456789abcdef;
    integer consumed=0;
    reg last_toggle=0;
    reg [63:0] consumed_value=0;
    reg monitor_enabled=0,selection_expected=0;
    realtime rtc_changed_at=0;
    always @(native_rtc)begin
        if(monitor_enabled && !reset && clk!==0)$fatal(1,"RTC bundle changed outside service negedge");
        rtc_changed_at=$realtime;
    end
    always @(posedge native_clk or posedge reset)begin
        if(reset)begin consumed<=0;last_toggle<=0;consumed_value<=0;end
        else begin
            if($realtime-rtc_changed_at<4)$fatal(1,"RTC bundle lacked setup before accepted native edge");
            if(native_rtc[64]!==last_toggle)begin
                consumed<=consumed+1;last_toggle<=native_rtc[64];consumed_value<=native_rtc[63:0];
            end
        end
    end
    always @(negedge clk)begin
        #0.2;
        if(monitor_enabled && !reset)begin
            if(io_strobe || ide_read || ide_write)$fatal(1,"RTC traffic emitted UIO/IDE strobe");
            if(selection_expected && (!io_uio || io_fpga))$fatal(1,"RTC traffic changed UIO selection");
            if(!status[28])$fatal(1,"D000 RTC capability bit28 missing");
        end
    end
    task tick;begin @(posedge clk);#1;end endtask
    task send(input [31:0] value);
        begin @(negedge control_clk);command=value;write=1;@(negedge control_clk);write=0;end
    endtask
    task complete(input expected_error);
        integer age;
        begin
            age=0;
            while(status[31] && age<80000)begin tick;age=age+1;end
            if(age==80000)$fatal(1,"RTC command exceeded bounded completion");
            if(status[30]!==expected_error)$fatal(1,"RTC command error expected%0d status%h",expected_error,status);
            repeat(8)tick;
        end
    endtask
    task op(input [31:0] value,input expected_error);
        begin send(value);complete(expected_error);end
    endtask
    task same_snapshot(input [64:0] value,input integer count);
        begin
            if(native_rtc!==value || consumed!=count)
                $fatal(1,"partial/rejected RTC changed snapshot expected%h/%0d got%h/%0d",value,count,native_rtc,consumed);
        end
    endtask
    task stage(input [63:0] value);
        reg [64:0] before_value;
        integer before_count;
        begin
            before_value=native_rtc;before_count=consumed;
            op({8'd5,value[23:0]},0);same_snapshot(before_value,before_count);
            op({8'd6,value[47:24]},0);same_snapshot(before_value,before_count);
        end
    endtask
    task commit(input [63:0] value,input expected_error);
        begin op({8'd7,8'd0,value[63:48]},expected_error);end
    endtask
    task halt_native;
        begin @(negedge clk);native_running=0;repeat(3)tick;if(chip_tick)$fatal(1,"fixture did not halt native timeline");end
    endtask
    integer age,before_count;
    reg completion_before;
    initial begin
        repeat(8)tick;@(negedge clk);reset=0;control_resetn=1;repeat(25)tick;monitor_enabled=1;
        if(native_rtc!==0 || !status[28])$fatal(1,"RTC reset/capability");
        op(32'h00000002,0);selection_expected=1;
        halt_native;
        stage(VALUE_A);completion_before=status[29];
        send({8'd7,8'd0,VALUE_A[63:48]});
        age=0;while(native_rtc!=={1'b1,VALUE_A} && age<1000)begin tick;age=age+1;end
        if(age==1000)$fatal(1,"complete RTC did not publish in inactive halfcycle");
        repeat(200)tick;
        if(!status[31] || status[29]!==completion_before || consumed!=0)$fatal(1,"stalled RTC falsely acknowledged native delivery");
        native_running=1;complete(0);
        if(consumed!=1 || consumed_value!==VALUE_A)$fatal(1,"RTC commit did not reach accepted native edge before ACK");

        // Out-of-order/high payload failures must also discard prior words.
        op(32'h06010203,1);same_snapshot({1'b1,VALUE_A},1);op(32'h04000000,0);
        op(32'h07001122,1);same_snapshot({1'b1,VALUE_A},1);op(32'h04000000,0);
        stage(VALUE_B);op({8'd7,8'h01,VALUE_B[63:48]},1);
        commit(VALUE_B,1);same_snapshot({1'b1,VALUE_A},1);op(32'h04000000,0);
        stage(VALUE_B);op(32'h08000000,1);commit(VALUE_B,1);
        same_snapshot({1'b1,VALUE_A},1);op(32'h04000000,0);
        stage(VALUE_B);op(32'h04000000,0);commit(VALUE_B,1);
        same_snapshot({1'b1,VALUE_A},1);op(32'h04000000,0);
        op({8'd5,VALUE_A[23:0]},0);op({8'd6,VALUE_A[47:24]},0);
        op({8'd5,VALUE_B[23:0]},0);commit(VALUE_B,1);
        same_snapshot({1'b1,VALUE_A},1);op(32'h04000000,0);
        $display("PASS AMIGA RTC capability,atomic3wordframing,rejectorder/highbits,restart/clear/badcommand,UIOselection");

        // The second commit toggles1->0 while stopped. A timeout must restore
        // the consumed1 snapshot; retry must still toggle0 and reach native.
        halt_native;stage(VALUE_B);send({8'd7,8'd0,VALUE_B[63:48]});
        age=0;while(native_rtc!=={1'b0,VALUE_B} && age<1000)begin tick;age=age+1;end
        if(age==1000)$fatal(1,"second RTC commit did not publish");
        complete(1);same_snapshot({1'b1,VALUE_A},1);
        native_running=1;repeat(30)tick;same_snapshot({1'b1,VALUE_A},1);
        op(32'h04000000,0);stage(VALUE_B);commit(VALUE_B,0);
        if(native_rtc!=={1'b0,VALUE_B} || consumed!=2 || consumed_value!==VALUE_B)
            $fatal(1,"RTC timeout/retry lost toggle or delivered wrong data");
        $display("PASS AMIGA RTC boundedtimeout restoreslastcommittedsnapshot andsecondcommitretryreachesnative");

        halt_native;stage(VALUE_C);send({8'd7,8'd0,VALUE_C[63:48]});
        age=0;while(native_rtc!=={1'b1,VALUE_C} && age<1000)begin tick;age=age+1;end
        if(age==1000)$fatal(1,"reset-pending RTC did not publish");
        selection_expected=0;reset=1;control_resetn=0;repeat(8)tick;
        if(native_rtc!==0)$fatal(1,"reset did not clear RTC output");
        @(negedge clk);reset=0;control_resetn=1;native_running=1;repeat(100)tick;
        same_snapshot(65'd0,0);
        op(32'h04000000,0);op({8'd6,VALUE_C[47:24]},1);op(32'h04000000,0);
        op({8'd7,8'd0,VALUE_C[63:48]},1);same_snapshot(65'd0,0);op(32'h04000000,0);
        op(32'h00000002,0);selection_expected=1;stage(VALUE_C);commit(VALUE_C,0);
        if(native_rtc!=={1'b1,VALUE_C} || consumed!=1 || consumed_value!==VALUE_C)$fatal(1,"RTC reset/retry failed");
        $display("PASS AMIGA RTC pendingresetclearsstage/output,negedgepublication,nativesetup,andfreshretry");
        $finish;
    end
    initial begin #5000000;$fatal(1,"RTC host watchdog");end
endmodule

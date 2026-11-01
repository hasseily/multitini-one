// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_amiga_display_host;
    reg clk=0,control_clk=0,reset=1,control_resetn=0,write=0;
    always #5 clk=~clk;
    always #7 control_clk=~control_clk;
    reg native_run=1,native_enable=1;
    always @(negedge clk)native_enable<=native_run;
    wire chip_tick=native_enable && !reset;
    wire native_clk;
    assign #0.1 native_clk=clk && native_enable;
    reg [31:0] command=0;
    wire [31:0] status;
    wire io_fpga,io_uio,io_strobe,ide_read,ide_write;
    wire [15:0] io_din,ide_wdata,native_display_request,display_read_data;
    wire [4:0] ide_address,display_read_address;
    wire [64:0] native_rtc;
    wire [1:0] capture_mode;
    wire display_switch_pending;
    reg display_quiescent=0;
    multitini_amiga_host #(.DISPLAY_TIMEOUT_CYCLES(128)) host_i(
        .control_clk(control_clk),.control_resetn(control_resetn),.write(write),.command(command),.status(status),
        .clk(clk),.reset(reset),.chip_tick(chip_tick),.io_fpga(io_fpga),.io_uio(io_uio),
        .io_strobe(io_strobe),.io_din(io_din),.io_wait(1'b0),.io_dout(16'd0),.ide_req(6'd0),
        .ide_address(ide_address),.ide_read(ide_read),.ide_write(ide_write),.ide_wdata(ide_wdata),.ide_rdata(16'd0),
        .native_rtc(native_rtc),.native_display_request(native_display_request),
        .display_read_address(display_read_address),.display_read_data(display_read_data),
        .capture_mode(capture_mode),.display_switch_pending(display_switch_pending),.display_quiescent(display_quiescent));
    reg [23:1] address=0;
    reg as_n=1,rw=1,uds_n=1,lds_n=1;
    reg [15:0] write_data=0;
    wire read_selected;
    wire [15:0] read_data;
    multitini_amiga_display_mailbox guest_i(.service_clk(clk),.native_clk(native_clk),
        .reset(reset),.guest_reset(1'b0),.request(native_display_request),.address(address),
        .as_n(as_n),.rw(rw),.uds_n(uds_n),.lds_n(lds_n),.dtack_n(1'b0),.write_data(write_data),
        .read_selected(read_selected),.read_data(read_data),.arm_address(display_read_address),.arm_data(display_read_data));
    task tick;begin @(posedge clk);#1;end endtask
    task send(input [31:0] value);
        begin @(negedge control_clk);command=value;write=1;@(negedge control_clk);write=0;end
    endtask
    task complete(input fail_expected);
        integer age;
        begin age=0;while(status[31] && age<100000)begin tick;age=age+1;end
            if(age==100000 || status[30]!==fail_expected)$fatal(1,"display host completion %h",status);
            repeat(6)tick;
        end
    endtask
    task op(input [31:0] value,input fail_expected);begin send(value);complete(fail_expected);end endtask
    task guest_write(input [4:0] index,input [15:0] value,input upper,input lower);
        begin @(negedge native_clk);address=(24'hde0f00>>1)+index;as_n=0;rw=0;
            write_data=value;uds_n=!upper;lds_n=!lower;
            @(negedge native_clk);as_n=1;rw=1;uds_n=1;lds_n=1;repeat(2)tick;
        end
    endtask
    task guest_read(input [23:0] byte_address,input selected,input [15:0] value);
        begin @(negedge native_clk);address=byte_address>>1;as_n=0;rw=1;uds_n=0;lds_n=0;
            #1;if(read_selected!==selected || (selected && read_data!==value))$fatal(1,"guest decode %h got%h expected%h",byte_address,read_data,value);
            @(negedge native_clk);as_n=1;uds_n=1;lds_n=1;
        end
    endtask
    task arm_read(input [4:0] index,input [15:0] value);
        begin op({8'd9,19'd0,index},0);if(status[15:0]!==value)$fatal(1,"ARM snapshot idx%0d got%h expected%h",index,status[15:0],value);end
    endtask
    reg monitoring=0,selected_uio=0;
    always @(capture_mode or native_display_request or display_switch_pending)
        if(monitoring && !reset && clk!==0)$fatal(1,"display bundle did not publish at negedge");
    always @(negedge clk)begin
        #0.2;if(monitoring && !reset)begin
            if(io_strobe || ide_read || ide_write)$fatal(1,"display command strobed native disk/UIO");
            if(selected_uio && (!io_uio || io_fpga))$fatal(1,"display changed UIO selection");
        end
    end
    initial begin
        repeat(8)tick;@(negedge clk);reset=0;control_resetn=1;repeat(25)tick;monitoring=1;
        if(!status[26] || !status[27] || !status[28])$fatal(1,"optional capabilities missing");
        op(32'h00000002,0);selected_uio=1;
        guest_read(24'hde0f00,1,16'h4d54);guest_read(24'hde0f02,1,1);guest_read(24'hde1000,0,0);
        guest_write(0,0,1,1);guest_read(24'hde0f00,1,16'h4d54);
        arm_read(0,16'h4d54);arm_read(1,1);arm_read(3,0);
        native_run=0;repeat(4)tick;send(32'h08000004);repeat(100)tick;
        if(!status[31] || native_display_request!==4)$fatal(1,"request acknowledged without native edge");
        native_run=1;complete(0);guest_read(24'hde0f04,1,4);
        guest_write(6,640,1,1);guest_write(7,256,1,1);guest_write(8,4,1,1);
        arm_read(6,640);arm_read(7,256);arm_read(3,0);
        guest_write(5,16'hab00,1,0);guest_write(5,16'h00cd,0,1);arm_read(5,16'habcd);
        guest_write(3,4,1,1);arm_read(3,4);
        op(32'h08000000,1);op(32'h04000000,0);
        op(32'h08000007,0);guest_read(24'hde0f04,1,7);op(32'h08000004,0);
        op(32'h08010004,1);op(32'h04000000,0);
        op(32'h09000012,1);op(32'h04000000,0);
        op(32'h0a000004,1);op(32'h04000000,0);
        $display("PASS AMIGA display guest MMIO, readonly signature, byte lanes, coherent snapshots, framing and capability");
        send(32'h0a000002);repeat(30)tick;
        if(!status[31] || !display_switch_pending || capture_mode!=0)$fatal(1,"profile changed before drain");
        display_quiescent=1;
        while(capture_mode!=2)tick;
        repeat(40)tick;
        if(!status[31] || !display_switch_pending)$fatal(1,"geometry settle hold released too soon");
        complete(0);
        if(capture_mode!=2 || display_switch_pending || status[15:0]!=2)$fatal(1,"drained profile commit/echo failed");
        op(32'h0a000003,0);
        if(capture_mode!=3 || display_switch_pending || status[15:0]!=3)$fatal(1,"wide profile commit/echo failed");
        op(32'h0a000002,0);
        display_quiescent=0;op(32'h0a000001,1);
        if(capture_mode!=2 || display_switch_pending)$fatal(1,"drain timeout changed old profile");
        op(32'h04000000,0);native_run=0;repeat(4)tick;op(32'h08000009,1);
        if(native_display_request!=4)$fatal(1,"unconsumed request did not roll back");
        op(32'h04000000,0);native_run=1;repeat(4)tick;op(32'h08000009,0);guest_read(24'hde0f04,1,9);
        selected_uio=0;reset=1;control_resetn=0;repeat(6)tick;
        if(native_display_request!=0 || capture_mode!=0 || display_switch_pending)$fatal(1,"display reset state");
        @(negedge clk);reset=0;control_resetn=1;repeat(30)tick;arm_read(3,0);arm_read(5,0);
        $display("PASS AMIGA display native-edge ACK, drain-before-commit, bounded failure, request rollback and reset");
        $finish;
    end
    initial begin #3000000;$fatal(1,"display watchdog");end
endmodule

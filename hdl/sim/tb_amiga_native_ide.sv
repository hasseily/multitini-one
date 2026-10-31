// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_amiga_native_ide;
    reg clk=0,control_clk=0,reset=1,control_resetn=0,write=0;
    always #5 clk=~clk;
    always #7 control_clk=~control_clk;
    reg [1:0] phase=0;
    reg native_running=1,native_enable=1;
    wire chip_tick=!reset && native_enable;
    always @(posedge clk)phase<=phase+1'b1;
    always @(negedge clk)native_enable<=native_running;
    wire native_clk;
    assign #0.1 native_clk=clk && native_enable;
    reg [31:0] command=0;
    wire [31:0] status;
    wire io_fpga,io_uio,io_strobe;
    wire [15:0] io_din;
    wire [4:0] ide_address;
    wire ide_read,ide_write;
    wire [15:0] ide_wdata,ide_rdata;
    wire [2:0] native_request;
    wire native_irq,native_drq;
    reg [3:0] guest_address=0;
    reg guest_read=0,guest_write=0;
    reg [31:0] guest_wdata=0;
    wire [31:0] guest_rdata;
    // Clock-to-Q/interconnect delay exposes pulses launched too close to a
    // native edge. A command must span a complete intended native sample.
    wire [4:0] delayed_address;
    wire [15:0] delayed_wdata;
    wire delayed_read,delayed_write;
    assign #1 delayed_address=ide_address;
    assign #1 delayed_wdata=ide_wdata;
    assign #1 delayed_read=ide_read;
    assign #1 delayed_write=ide_write;
    multitini_amiga_host host_i(.control_clk(control_clk),.control_resetn(control_resetn),
        .write(write),.command(command),.status(status),.clk(clk),.reset(reset),.chip_tick(chip_tick),
        .io_fpga(io_fpga),.io_uio(io_uio),.io_strobe(io_strobe),.io_din(io_din),
        .io_wait(1'b0),.io_dout(16'd0),.ide_req({3'd0,native_request}),
        .ide_address(ide_address),.ide_read(ide_read),.ide_write(ide_write),
        .ide_wdata(ide_wdata),.ide_rdata(ide_rdata));
    ide native_i(.clk(native_clk),.rst_n(!reset),.irq(native_irq),.drq(native_drq),.use_fast(1'b0),.no_data(),
        .drive_en(),.io_address(guest_address),.io_read(guest_read),.io_readdata(guest_rdata),.io_write(guest_write),
        .io_writedata(guest_wdata),.io_32(1'b0),.io_wait(),.request(native_request),
        .mgmt_address(delayed_address[3:0]),.mgmt_write(delayed_write),.mgmt_writedata(delayed_wdata),
        .mgmt_read(delayed_read),.mgmt_readdata(ide_rdata));
    task tick;begin @(posedge clk);#1;end endtask
    integer transaction_number=0;
    reg [3:0] arrival_phases=0;
    always @(posedge clk)if(!reset && host_i.state==0 && host_i.request_valid)arrival_phases[phase]<=1;
    task op(input [7:0] code,input [4:0] address,input [15:0] value);
        integer timeout;
        begin
            if(transaction_number%17==0)begin native_running=0;repeat(2)tick;end
            @(negedge control_clk);command={code,3'd0,address,value};write=1;
            @(negedge control_clk);write=0;
            if(transaction_number%17==0)begin
                timeout=0;while(host_i.state!=3 && timeout<1000)begin tick;timeout=timeout+1;end
                if(timeout==1000)$fatal(1,"host failed to arm while native clock held");
                repeat(7)tick;
                if(ide_read || ide_write)$fatal(1,"host strobe escaped ARM while native clock held");
                native_running=1;
            end else if(transaction_number%19==0)begin
                timeout=0;while(host_i.state!=1 && timeout<1000)begin tick;timeout=timeout+1;end
                if(timeout==1000)$fatal(1,"host failed to enter PULSE");
                native_running=0;repeat(7)tick;
                if(!(ide_read || ide_write))$fatal(1,"host pulse disappeared during native hold");
                native_running=1;
            end
            timeout=0;
            while(status[31] && timeout<1000)begin tick;timeout=timeout+1;end
            if(timeout==1000 || status[30])$fatal(1,"native IDE command failed op%0d addr%0d status%h",code,address,status);
            repeat(4+(transaction_number%7))tick;transaction_number=transaction_number+1;
        end
    endtask
    task guest_write_register(input [3:0] address,input [7:0] value);
        begin
            @(negedge native_clk);guest_address=address;guest_wdata={24'd0,value};guest_write=1;
            @(posedge native_clk);#1;
            @(negedge native_clk);guest_write=0;repeat(2)tick;
        end
    endtask
    task guest_read_register(input [3:0] address,output [7:0] value);
        begin
            @(negedge native_clk);guest_address=address;guest_read=1;
            @(posedge native_clk);#1;value=guest_rdata[7:0];
            @(negedge native_clk);guest_read=0;repeat(2)tick;
        end
    endtask
    task expect_irq(input value,input [255:0] context_text);
        begin if(native_irq!==value)$fatal(1,"native IDE IRQ expected%0d after%s got%0d",value,context_text,native_irq);end
    endtask
    integer word_number;
    reg [15:0] expected;
    reg [7:0] guest_value;
    initial begin
        repeat(12)tick;@(negedge clk);reset=0;control_resetn=1;repeat(25)tick;
        op(3,1,16'h4d02);op(2,1,0);
        if(status[15:0]!==16'h4d02)$fatal(1,"native IDE register read returned%h",status[15:0]);
        // Populate a complete sector through the management FIFO, then reset
        // its pointer through register14. Each read must return its own word
        // before falling mgmt_read advances the native pointer.
        op(2,14,0);
        for(word_number=0;word_number<256;word_number=word_number+1)
            op(3,15,16'h1200^word_number);
        op(2,14,0);
        for(word_number=0;word_number<256;word_number=word_number+1)begin
            expected=16'h1200^word_number;
            op(2,15,0);
            if(status[15:0]!==expected)$fatal(1,"native IDE FIFO word%0d expected%h got%h",word_number,expected,status[15:0]);
        end
        if(arrival_phases!=4'b1111)$fatal(1,"native IDE fixture missed command arrival phases:%b",arrival_phases);

        // Reset acknowledgement retains ATA error signature1 but neither ERR
        // status nor INTRQ. Error completion uses0x5500, adding ERR and INTRQ.
        op(3,6,16'h0009); // drive0 present
        op(3,0,16'h0100);op(3,5,16'h5000);
        expect_irq(0,"reset acknowledgement");
        guest_read_register(1,guest_value);
        if(guest_value!==8'h01)$fatal(1,"IDE reset error signature%h",guest_value);
        guest_read_register(7,guest_value);
        if(guest_value!==8'h50)$fatal(1,"IDE reset status%h",guest_value);
        guest_write_register(7,8'hec);
        if(native_request!==3'b100)$fatal(1,"guest IDE command request missing");
        op(3,0,16'h0400);op(3,5,16'h5500);
        expect_irq(1,"ABRT error completion");
        guest_read_register(14,guest_value);
        if(guest_value!==8'h51 || native_drq)$fatal(1,"IDE error altstatus/DRQ%h/%b",guest_value,native_drq);
        expect_irq(1,"alternate-status read");
        guest_read_register(1,guest_value);
        if(guest_value!==8'h04)$fatal(1,"IDE ABRT error register%h",guest_value);
        expect_irq(1,"error-register read");
        guest_read_register(7,guest_value);
        if(guest_value!==8'h51)$fatal(1,"IDE error status%h",guest_value);
        expect_irq(0,"regular-status read");

        guest_write_register(14,8'h02); // nIEN suppresses a new completion IRQ
        guest_write_register(7,8'hec);op(3,0,16'h1000);op(3,5,16'h5500);
        expect_irq(0,"nIEN masked error completion");
        guest_read_register(14,guest_value);
        if(guest_value!==8'h51)$fatal(1,"masked IDE error status%h",guest_value);
        guest_read_register(1,guest_value);
        if(guest_value!==8'h10)$fatal(1,"IDE IDNF error register%h",guest_value);
        guest_write_register(14,8'h00);
        expect_irq(0,"nIEN release without new completion");
        guest_write_register(7,8'hec);op(3,5,16'h5500);
        expect_irq(1,"reenabled error completion");
        guest_read_register(7,guest_value);expect_irq(0,"status clears reenabled IRQ");
        $display("PASS AMIGA native IDE registers/256wordFIFO alldelayedphases plusreset5000/error5500,ERR1,nIEN,altstatuspreserve/statusclearIRQ");
        $finish;
    end
    initial begin #5000000;$fatal(1,"native IDE watchdog");end
endmodule

// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_genesis_eeprom;
    reg clk=0;
    always #5 clk=~clk;
    reg reset=1;
    reg [1:0] mode=1;
    reg [12:0] address_mask=13'hff;
    reg master_sda=1,scl=1;
    wire slave_sda;
    wire bus_sda=master_sda && slave_sda;
    wire [12:0] iaddr;
    wire [7:0] idata;
    wire iwrite;
    reg [7:0] iq;
    reg [7:0] imem[0:8191];
    multitini_genesis_i2c_eeprom i2c_i(.clk(clk),.reset(reset),.mode(mode),.address_mask(address_mask),
        .sda_in(bus_sda),.scl(scl),.sda_out(slave_sda),.ram_addr(iaddr),.ram_q(iq),.ram_data(idata),.ram_write(iwrite));
    reg select_n=1,hold_n=1,sck=0,si=0;
    wire so;
    wire [11:0] saddr;
    wire [7:0] sdata;
    wire swrite;
    reg [7:0] sq;
    reg [7:0] smem[0:4095];
    multitini_genesis_spi_eeprom spi_i(.clk(clk),.reset(reset),.select_n(select_n),.hold_n(hold_n),
        .sck(sck),.si(si),.so(so),.ram_addr(saddr),.ram_q(sq),.ram_data(sdata),.ram_write(swrite));
    always @(posedge clk) begin
        iq<=imem[iaddr];if(iwrite)imem[iaddr]<=idata;
        sq<=smem[saddr];if(swrite)smem[saddr]<=sdata;
    end
    task automatic line(input bit clock_value,input bit data_value);
        @(negedge clk);scl=clock_value;master_sda=data_value;
        repeat(8)@(negedge clk);
    endtask
    task automatic start;
        line(0,1);line(1,1);line(1,0);line(0,0);
    endtask
    task automatic stop;
        line(0,0);line(1,0);line(1,1);
    endtask
    task automatic send(input [7:0] value);
        for(integer b=7;b>=0;b=b-1)begin line(0,value[b]);line(1,value[b]);line(0,value[b]);end
        line(0,1);line(1,1);
        if(bus_sda!==0)$fatal(1,"I2C no ACK for %h, state %0d",value,i2c_i.state);
        line(0,1);
    endtask
    task automatic receive(output reg [7:0] value,input bit acknowledge);
        for(integer b=7;b>=0;b=b-1)begin line(1,1);value[b]=bus_sda;line(0,1);end
        line(0,!acknowledge);line(1,!acknowledge);line(0,!acknowledge);line(0,1);
    endtask
    task automatic spi_line(input bit clock_value,input bit data_value);
        @(negedge clk);sck=clock_value;si=data_value;
        repeat(8)@(negedge clk);
    endtask
    task automatic spi_send(input [7:0] value);
        for(integer b=7;b>=0;b=b-1)begin spi_line(0,value[b]);spi_line(1,value[b]);spi_line(0,value[b]);end
    endtask
    task automatic spi_receive(output reg [7:0] value);
        for(integer b=7;b>=0;b=b-1)begin spi_line(1,0);value[b]=so;spi_line(0,0);end
    endtask
    task automatic spi_select(input bit active);
        @(negedge clk);select_n=!active;sck=0;repeat(8)@(negedge clk);
    endtask
    reg [7:0] value;
    initial begin
        for(integer j=0;j<8192;j=j+1)imem[j]=8'hff;
        for(integer j=0;j<4096;j=j+1)smem[j]=8'hff;
        repeat(8)@(negedge clk);reset=0;
        // Standard 24C02, eight-byte page wraps within its current page.
        start;send(8'ha0);send(8'h07);send(8'ha1);send(8'hb2);stop;
        if(imem[7]!==8'ha1 || imem[0]!==8'hb2)$fatal(1,"24C02 page wrap %h %h",imem[7],imem[0]);
        start;send(8'ha0);send(8'h07);start;send(8'ha1);receive(value,0);stop;
        if(value!==8'ha1)$fatal(1,"24C02 random read %h",value);
        // Repeated-start device control must not overwrite 24C65 A12:A8.
        mode=2;address_mask=13'h1fff;
        start;send(8'ha0);send(8'h12);send(8'h34);send(8'hac);send(8'h53);stop;
        start;send(8'ha0);send(8'h12);send(8'h34);start;send(8'ha1);
        receive(value,1);if(value!==8'hac)$fatal(1,"24C65 repeated-start read %h",value);
        receive(value,0);stop;if(value!==8'h53)$fatal(1,"24C65 sequential read %h",value);
        // Bank bits in a 24C16 control byte select the high address bits.
        mode=1;address_mask=13'h7ff;
        start;send(8'haa);send(8'h23);send(8'h69);stop;
        start;send(8'haa);send(8'h23);start;send(8'hab);receive(value,0);stop;
        if(value!==8'h69 || imem[13'h523]!==8'h69)$fatal(1,"24C16 bank read");
        // X24C01 packs its seven address bits and direction into one byte.
        mode=0;address_mask=13'h7f;
        start;send(8'h6a);send(8'h77);stop;
        start;send(8'h6b);receive(value,0);stop;
        if(value!==8'h77)$fatal(1,"X24C01 read %h",value);
        // SPI write protection, write enable, page wrapping and read stream.
        spi_select(1);spi_send(8'h02);spi_send(8'h00);spi_send(8'h1f);spi_send(8'h11);spi_select(0);
        if(smem[31]!==8'hff)$fatal(1,"SPI wrote without WREN");
        spi_select(1);spi_send(8'h06);spi_select(0);
        spi_select(1);spi_send(8'h02);spi_send(8'h00);spi_send(8'h1f);spi_send(8'hd4);spi_send(8'he5);spi_select(0);
        if(smem[31]!==8'hd4 || smem[0]!==8'he5)$fatal(1,"SPI page wrap");
        spi_select(1);spi_send(8'h03);spi_send(8'h00);spi_send(8'h1f);
        hold_n=0;repeat(20)@(negedge clk);hold_n=1;
        spi_receive(value);if(value!==8'hd4)$fatal(1,"SPI read %h",value);
        spi_receive(value);spi_select(0);if(value!==8'hff)$fatal(1,"SPI sequential read must cross page %h",value);
        spi_select(1);spi_send(8'h03);spi_send(8'h00);spi_send(8'h00);spi_receive(value);spi_select(0);
        if(value!==8'he5)$fatal(1,"SPI wrapped write readback");
        $display("PASS Genesis original I2C/SPI EEPROM protocols, banked/repeated-start reads, page wrap, write protection and hold");
        $finish;
    end
    initial begin #10000000;$fatal(1,"EEPROM test timed out");end
endmodule

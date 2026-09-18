// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Independent byte-addressed LY68L6400 subset, derived from the public
// manufacturer's rev1.2 specification, not from controller tape positions.
// https://www.lyontek.com.tw/pdf/ddr/LY68L6400-1.2.pdf
// Implements SPI/QPI reset, SPI QPI entry, wrap toggle, QPI02/EB, and actual
// six-dummy-cycle/read-on-falling-edge pin protocol. No abstract command hooks.
module ly68l6400_model #(
    parameter real OUTPUT_DELAY=6.0,
    parameter integer WARM_QPI=0
)(input wire clk,ce_n,allow_abort, inout wire [3:0] io,
  output reg qpi=WARM_QPI!=0, wrapped=0,
  output reg [31:0] reads=0,writes=0,resets=0,
  output reg [23:0] last_address=0);
    reg [7:0] memory[0:8388607];
    reg [3:0] out_nibble=0;
    reg driving=0, reset_enabled=0;
    reg transaction_qpi;
    reg [7:0] command;
    reg [23:0] address;
    reg [3:0] high_nibble;
    integer edges=0, data_nibbles=0, read_nibbles=0;
    integer command_edges, address_end;
    integer byte_address;
    realtime selected_at=0, deselected_at=0, rise_at=0, fall_at=0;
    realtime io_changed_at=0,hold_until=0;
    realtime min_high=1.0e9,max_low=0,min_hold=1.0e9;
    assign io=driving ? out_nibble : 4'bzzzz;
    always @(io) begin
        if($realtime<hold_until && !allow_abort)
            $fatal(1,"PSRAM input hold shorter than2ns");
        io_changed_at=$realtime;
    end

    function integer next_address(input integer offset);
        if(wrapped) next_address=(address & 24'hffffe0)|((address+offset)&31);
        else next_address=(address+offset)&24'h7fffff;
    endfunction

    always @(negedge ce_n) begin
        if(deselected_at!=0 && $realtime-deselected_at < 49.999)
            $fatal(1,"PSRAM tCPH too short: %0.3f ns",$realtime-deselected_at);
        if(deselected_at!=0 && $realtime-deselected_at<min_high)
            min_high=$realtime-deselected_at;
        selected_at=$realtime;
        transaction_qpi=qpi; command=0;address=0;edges=0;
        command_edges=qpi ? 2 : 8;
        address_end=command_edges+6;
        data_nibbles=0;read_nibbles=0;driving=0;
    end

    always @(posedge clk) if(!ce_n) begin
        if(edges==0 && $realtime-selected_at<2.499)
            $fatal(1,"PSRAM tCSP too short");
        if(edges!=0 && $realtime-rise_at<7.0)
            $fatal(1,"PSRAM period too short");
        if(edges!=0 && $realtime-fall_at<4.68)
            $fatal(1,"PSRAM low clock pulse too short");
        rise_at=$realtime;
        if(edges<command_edges || ((command==8'h02 || command==8'heb) && edges<address_end) ||
            (command==8'h02 && edges>=address_end)) begin
            if($realtime-io_changed_at<1.999)
                $fatal(1,"PSRAM input setup shorter than2ns");
            hold_until=$realtime+2.0;
        end
        if(edges<command_edges)
            command=transaction_qpi ? {command[3:0],io} : {command[6:0],io[0]};
        else if(command==8'h02 || command==8'heb) begin
            if(!transaction_qpi) $fatal(1,"Memory operation before QPI entry");
            if(edges<address_end) begin
                address={address[19:0],io};
                if(edges==address_end-1) begin
                    if(address[23] || address[4:0]!=0 || !wrapped)
                        $fatal(1,"Invalid address/wrap mode: %h wrap=%b",address,wrapped);
                    last_address=address;
                end
            end else if(command==8'h02) begin
                if((^io)===1'bx) $fatal(1,"Unknown/contention write nibble");
                if((data_nibbles&1)==0) high_nibble=io;
                else memory[next_address(data_nibbles/2)]={high_nibble,io};
                data_nibbles=data_nibbles+1;
            end else if(edges<address_end+6 && io!==4'bzzzz)
                $fatal(1,"Controller drives during read dummy cycles");
        end
        edges=edges+1;
    end

    always @(negedge clk) if(!ce_n) begin
        if($realtime-rise_at<4.68 && !allow_abort)
            $fatal(1,"PSRAM high clock pulse too short: %0.3f ns",$realtime-rise_at);
        fall_at=$realtime;
        if(command==8'heb && transaction_qpi && edges>=address_end+6) begin
            byte_address=next_address(read_nibbles/2);
            if((read_nibbles&1)==0) out_nibble <= #(OUTPUT_DELAY) memory[byte_address][7:4];
            else out_nibble <= #(OUTPUT_DELAY) memory[byte_address][3:0];
            driving <= #(OUTPUT_DELAY) 1;
            read_nibbles=read_nibbles+1;
        end
    end

    always @(posedge ce_n) if(selected_at!=0) begin
        if($realtime-selected_at>8000.0) $fatal(1,"PSRAM tCEM exceeded");
        if($realtime-selected_at>max_low) max_low=$realtime-selected_at;
        if(!allow_abort) begin
            if($realtime-rise_at<19.999) $fatal(1,"PSRAM tCHD too short: %0.3f ns",$realtime-rise_at);
            if($realtime-rise_at<min_hold) min_hold=$realtime-rise_at;
        end
        deselected_at=$realtime;
        driving<=#6.0 0;
        // A short command in the wrong bus mode is ignored, as during warm
        // recovery's initial quad commands when a chip already is in SPI.
        if(edges>=command_edges && !allow_abort) begin
            if(command==8'h99 && reset_enabled) begin
                qpi=0;wrapped=0;resets=resets+1;reset_enabled=0;
            end else begin
                reset_enabled=command==8'h66;
                if(command==8'h35 && !transaction_qpi) qpi=1;
                if(command==8'hc0) wrapped=!wrapped;
                if(command==8'h02) begin
                    if(data_nibbles!=64) $fatal(1,"Write length %0d nibbles",data_nibbles);
                    writes=writes+1;
                end
                if(command==8'heb) begin
                    // The final clock's falling edge can expose the next high
                    // nibble; only64 are sampled by the controller.
                    if(edges!=78 || read_nibbles!=65)
                        $fatal(1,"Read clock/dummy count %0d/%0d",edges,read_nibbles);
                    reads=reads+1;
                end
            end
        end
    end
endmodule

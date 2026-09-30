// SPDX-License-Identifier: GPL-3.0-only
// Original serial EEPROM bus models. The backing store is the cartridge save
// RAM; no game data or device ROM contents are included.
module multitini_genesis_i2c_eeprom (
    input wire clk, reset,
    input wire [1:0] mode, // 0: X24C01, 1: one address byte, 2: two bytes
    input wire [12:0] address_mask,
    input wire sda_in, scl,
    output reg sda_out,
    output reg [12:0] ram_addr,
    input wire [7:0] ram_q,
    output reg [7:0] ram_data,
    output reg ram_write
);
    localparam IDLE=0, RECEIVE=1, ACK=2, READ_WAIT=3, READ=4,
               READ_RELEASE=5, READ_ACK=6, READ_ACK_END=7;
    localparam CONTROL=0, ADDRESS_HIGH=1, ADDRESS_LOW=2, DATA=3;
    reg [2:0] state=IDLE, after_ack=IDLE;
    reg [1:0] eeprom_context=CONTROL;
    reg [2:0] bit_count;
    reg [7:0] shift, read_shift;
    reg sda_previous=1, scl_previous=1, ack_clock, increment_after_ack;
    wire rise = !scl_previous && scl;
    wire fall = scl_previous && !scl;
    wire start = sda_previous && !sda_in && scl && scl_previous;
    wire stop = !sda_previous && sda_in && scl && scl_previous;
    wire [7:0] received = {shift[6:0],sda_in};
    wire [12:0] page_mask = mode==0 ? 13'd3 : mode==2 ? 13'd31 :
                                   address_mask<=13'hff ? 13'd7 : 13'd15;
    always @(posedge clk) begin
        sda_previous <= sda_in;
        scl_previous <= scl;
        ram_write <= 0;
        if(reset) begin
            state <= IDLE;
            sda_out <= 1;
            ram_addr <= 0;
            sda_previous <= 1;
            scl_previous <= 1;
        end else if(start) begin
            state <= RECEIVE;
            eeprom_context <= CONTROL;
            bit_count <= 0;
            sda_out <= 1;
        end else if(stop) begin
            state <= IDLE;
            sda_out <= 1;
        end else begin
            case(state)
                RECEIVE: if(rise) begin
                    shift <= received;
                    if(bit_count != 7) bit_count <= bit_count + 1'b1;
                    else begin
                        bit_count <= 0;
                        state <= ACK;
                        ack_clock <= 0;
                        after_ack <= RECEIVE;
                        increment_after_ack <= 0;
                        case(eeprom_context)
                            CONTROL: begin
                                if(mode==0) begin
                                    ram_addr <= {6'd0,received[7:1]} & address_mask;
                                    eeprom_context <= DATA;
                                    if(received[0]) after_ack <= READ_WAIT;
                                end else if(received[7:4] != 4'ha) begin
                                    state <= IDLE; // another I2C device
                                end else begin
                                    if(mode==1) begin
                                        ram_addr[10:8] <= received[3:1] & address_mask[10:8];
                                        ram_addr[12:11] <= 0;
                                    end
                                    if(received[0]) after_ack <= READ_WAIT;
                                    else eeprom_context <= mode==2 ? ADDRESS_HIGH : ADDRESS_LOW;
                                end
                            end
                            ADDRESS_HIGH: begin
                                ram_addr[12:8] <= received[4:0] & address_mask[12:8];
                                eeprom_context <= ADDRESS_LOW;
                            end
                            ADDRESS_LOW: begin
                                ram_addr[7:0] <= received & address_mask[7:0];
                                eeprom_context <= DATA;
                            end
                            DATA: begin
                                ram_data <= received;
                                ram_write <= 1;
                                increment_after_ack <= 1;
                            end
                        endcase
                    end
                end
                ACK: begin
                    if(rise) ack_clock <= 1;
                    if(fall) begin
                        if(!ack_clock) sda_out <= 0;
                        else begin
                            sda_out <= 1;
                            state <= after_ack;
                            // Increment only after RAM observed the write.
                            if(increment_after_ack)
                                ram_addr <= ((ram_addr & ~page_mask) |
                                            ((ram_addr+1'b1) & page_mask)) & address_mask;
                        end
                    end
                end
                READ_WAIT: begin
                    read_shift <= ram_q;
                    sda_out <= ram_q[7];
                    bit_count <= 0;
                    state <= READ;
                end
                READ: begin
                    if(rise) begin
                        if(bit_count==7) state <= READ_RELEASE;
                        else bit_count <= bit_count+1'b1;
                    end
                    if(fall) begin
                        read_shift <= {read_shift[6:0],1'b1};
                        sda_out <= read_shift[6];
                    end
                end
                READ_RELEASE: if(fall) begin
                    sda_out <= 1;
                    state <= READ_ACK;
                end
                READ_ACK: if(rise) begin
                    if(sda_in) state <= IDLE;
                    else begin
                        ram_addr <= (ram_addr+1'b1) & address_mask;
                        state <= READ_ACK_END;
                    end
                end
                READ_ACK_END: if(fall) state <= READ_WAIT;
                default: sda_out <= 1;
            endcase
        end
    end
endmodule

module multitini_genesis_spi_eeprom (
    input wire clk, reset,
    input wire select_n, hold_n, sck, si,
    output reg so,
    output reg [11:0] ram_addr,
    input wire [7:0] ram_q,
    output reg [7:0] ram_data,
    output reg ram_write
);
    localparam COMMAND=0, ADDRESS_HIGH=1, ADDRESS_LOW=2, WRITE=3, READ=4, STATUS=5, IGNORE=6;
    reg [2:0] state=COMMAND;
    reg [7:0] shift, command;
    reg [2:0] bit_count;
    reg write_enabled=0, old_sck=0, old_select=1;
    wire rise=!old_sck && sck;
    wire fall=old_sck && !sck;
    wire [7:0] received={shift[6:0],si};
    always @(posedge clk) begin
        old_sck <= sck;
        old_select <= select_n;
        ram_write <= 0;
        if(reset) begin
            state <= COMMAND;
            bit_count <= 0;
            so <= 1;
            write_enabled <= 0;
            ram_addr <= 0;
        end else if(select_n) begin
            state <= COMMAND;
            bit_count <= 0;
            so <= 1;
            if(!old_select && state==WRITE) write_enabled <= 0;
        end else if(hold_n) begin
            if(rise) begin
                shift <= received;
                bit_count <= bit_count+1'b1;
                if(bit_count==7) begin
                    case(state)
                        COMMAND: begin
                            command <= received;
                            case(received)
                                8'h06: begin write_enabled<=1; state<=IGNORE; end
                                8'h04: begin write_enabled<=0; state<=IGNORE; end
                                8'h05: state<=STATUS;
                                8'h03,8'h02: state<=ADDRESS_HIGH;
                                default: state<=IGNORE;
                            endcase
                        end
                        ADDRESS_HIGH: begin ram_addr[11:8]<=received[3:0]; state<=ADDRESS_LOW; end
                        ADDRESS_LOW: begin ram_addr[7:0]<=received; state<=command==3 ? READ : WRITE; end
                        WRITE: if(write_enabled) begin ram_data<=received; ram_write<=1; end
                        READ: ram_addr<=ram_addr+1'b1;
                        default: ;
                    endcase
                end
            end
            // Address is advanced one edge after a write, preserving the
            // synchronous RAM address through its write-enable pulse.
            if(ram_write) ram_addr <= {ram_addr[11:5],ram_addr[4:0]+5'd1};
            if(fall) begin
                if(state==READ) so <= ram_q[7-bit_count];
                else if(state==STATUS) so <= bit_count==6 && write_enabled;
                else so <= 1;
            end
        end
    end
endmodule

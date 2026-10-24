// SPDX-License-Identifier: GPL-3.0-only
// No cartridge may enter turbo in M0-M6. Exact mode always uses native PLA.
module multitini_c64_island_map #(
    parameter FAST_VIC_IO=1, FAST_SID_IO=1, FAST_COLOUR_RAM=1,
    parameter FORWARD_PORT_WRITES=0, FF00_SNOOP=1
) (
    input wire clk, reset, port_write_retired,
    input wire [7:0] port_pins,
    input wire [15:0] addr,
    input wire write,
    output wire map_ok,
    output reg [2:0] read_source,
    output reg local_cycle, local_write, port_write, colour_cycle,
    output reg fio_cycle, forward_cycle, forward_ioonly, ff00_local,
    output reg forward_ack,
    output wire ram_store, colour_store
);
    localparam RAM=0, ROM=1, CHR=2, IO=3;
    reg [1:0] page[0:15];
    reg [2:0] port_q;
    reg valid=0;
    integer p;
    assign map_ok = valid;
    // Store enables need only the mapped page and device subpage. Keep them
    // independent of the read-data/forward priority mux and low-address
    // port/FF00 tests, except when the corresponding build option needs one.
    assign ram_store = write && page[addr[15:12]] != IO &&
        (!FORWARD_PORT_WRITES || addr[15:1] != 0) &&
        (FF00_SNOOP || addr != 16'hff00);
    assign colour_store = write && page[addr[15:12]] == IO &&
        addr[11:10] == 2'b10 && FAST_COLOUR_RAM;
    always @(posedge clk) begin
        port_q <= port_pins[2:0];
        if (reset) valid <= 0;
        else begin
            // Drop eligibility on the very edge that updates the 6510 port;
            // the next edge computes the new map and reopens eligibility.
            valid <= !port_write_retired;
            for (p=0;p<16;p=p+1) page[p] <= RAM;
            page[10] <= (&port_pins[1:0]) ? ROM : RAM;
            page[11] <= (&port_pins[1:0]) ? ROM : RAM;
            page[13] <= !(|port_pins[1:0]) ? RAM : port_pins[2] ? IO : CHR;
            page[14] <= port_pins[1] ? ROM : RAM;
            page[15] <= port_pins[1] ? ROM : RAM;
        end
    end
    // read_source: 0 RAM low, 1 RAM high, 2 ROM, 3 CHAR, 4 IO2, 5 B0.
    always @* begin
        read_source = {2'b00,addr[15]};
        local_cycle=1; local_write=write; port_write=0; colour_cycle=0;
        fio_cycle=0; forward_cycle=0; forward_ioonly=0;
        ff00_local=0; forward_ack=0;
        if (!write && page[addr[15:12]] == ROM) read_source=2;
        if (!write && page[addr[15:12]] == CHR) read_source=3;
        if (addr[15:1] == 0 && write) begin
            port_write=1;
            if (FORWARD_PORT_WRITES) begin
                local_cycle=0; local_write=0; forward_cycle=1; read_source=5;
            end
        end else if (write && addr == 16'hff00) begin
            local_cycle=0; local_write=0; forward_cycle=1;
            ff00_local=FF00_SNOOP; forward_ioonly=FF00_SNOOP; read_source=5;
        end else if (page[addr[15:12]] == IO) begin
            if (addr[11:10] == 2'b10 && FAST_COLOUR_RAM) begin
                colour_cycle=1; read_source=4;
            end else if (addr[11:10] < 2 &&
                         (addr[10] ? FAST_SID_IO : FAST_VIC_IO)) begin
                local_cycle=0; local_write=0; fio_cycle=1; read_source=4;
            end else begin
                local_cycle=0; local_write=0; forward_cycle=1; read_source=5;
                forward_ioonly=(addr[11:10] != 2'b10);
                forward_ack=(!write && addr[11:9]==3'b110 && addr[3:0]==4'hd) ||
                    (!write && addr[11:8]==4'hf && addr[4:0]==0) ||
                    (write && addr[11:8]==4'hf && addr[4:0]==5'h1f && addr[7:5]!=0) ||
                    (write && addr[11:10]==0 && (addr[5:0]==6'h19 || addr[5:0]==6'h1a));
            end
        end
    end
endmodule

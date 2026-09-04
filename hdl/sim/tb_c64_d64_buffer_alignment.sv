// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// c1541_gcr's iecdrv_mem delays its write enable and address by one sd_clk.
// c1541_gcr must align the matching data internally even when the producer
// changes address and data on every cycle. Exercise consecutive cache-copy
// writes, including the 255/256-byte boundary where a one-byte skew corrupts
// the next sector.
module tb_c64_d64_buffer_alignment;
    localparam integer COPY_BYTES = 514;

    logic clk = 1'b0;
    logic reset = 1'b1;
    always #5 clk = ~clk;

    logic [12:0] cache_address = 13'd0;
    logic [7:0]  cache_data = 8'd0;
    logic        cache_write = 1'b0;
    wire [7:0]   cache_read_data;

    wire [7:0] unused_dout;
    wire       unused_sync_n;
    wire       unused_byte_n;
    wire       unused_disk_write;

    c1541_gcr dut (
        .clk(clk),
        .ce(1'b0),
        .dout(unused_dout),
        .din(8'd0),
        .mode(1'b1),
        .mtr(1'b0),
        .freq(2'b00),
        .sync_n(unused_sync_n),
        .byte_n(unused_byte_n),
        .track(6'd18),
        .busy(1'b1),
        .we(unused_disk_write),
        .sd_clk(clk),
        .sd_lba(32'd0),
        .sd_buff_addr(cache_address),
        .sd_buff_dout(cache_data),
        .sd_buff_din(cache_read_data),
        .sd_buff_wr(cache_write)
    );

    function automatic [7:0] expected_byte(input integer address);
        expected_byte = ((address * 73) + 8'h5a) & 8'hff;
    endfunction

    task automatic check_address(input integer address);
        begin
            @(negedge clk);
            cache_address = address[12:0];
            // iecdrv_mem registers the read address before returning the byte.
            repeat (2) @(posedge clk);
            #1;
            if (cache_read_data !== expected_byte(address)) begin
                $display("FAIL: D64 buffer[%0d] expected %02x, got %02x",
                         address, expected_byte(address), cache_read_data);
                $fatal(1);
            end
        end
    endtask

    initial begin
        repeat (3) @(posedge clk);
        @(negedge clk);
        reset = 1'b0;

        for (integer address = 0; address < COPY_BYTES;
             address = address + 1) begin
            cache_write = 1'b1;
            cache_address = address[12:0];
            cache_data = expected_byte(address);
            @(posedge clk);
            @(negedge clk);
        end

        // Flush iecdrv_mem's delayed final write before changing to reads.
        cache_write = 1'b0;
        cache_data = 8'd0;
        @(posedge clk);

        check_address(0);
        check_address(1);
        check_address(254);
        check_address(255);
        check_address(256);
        check_address(257);
        check_address(511);
        check_address(512);
        check_address(513);

        $display("PASS: consecutive D64 cache bytes retain address/data alignment");
        $finish;
    end
endmodule

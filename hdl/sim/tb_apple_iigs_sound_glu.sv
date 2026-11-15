// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ns
module tb_apple_iigs_sound_glu;
    localparam logic [1:0] CONTROL = 0, DATA = 1, ADDR_LOW = 2, ADDR_HIGH = 3;

    logic clk = 0, reset = 1, select = 0, write = 0;
    logic [7:0] host_input = 0;
    logic [1:0] host_addr = 0;
    logic slot = 0;
    wire busy, ram_read_select, ram_write, doc_write, doc_host_en;
    wire [7:0] host_output, peek, sound_input, ram_output;
    wire [15:0] host_ram_addr;
    wire [15:0] ram_addr = slot ? 16'h8844 : host_ram_addr;
    logic [7:0] doc_output = 0;
    logic [7:0] doc_regs [256];

    always #5 clk = ~clk;

    soundglu glu(
        .CLK_14M(clk), .clk_7M_en(1'b0), .ph0_en(select), .reset(reset),
        .select(select), .wr(write), .host_addr(host_addr), .host_data_in(host_input),
        .sound_data_in(ram_read_select ? ram_output : doc_output), .osc_en(slot),
        .ram_access(ram_read_select), .host_data_out(host_output), .host_read_peek(peek),
        .host_busy(busy), .sound_addr(host_ram_addr), .sound_data_out(sound_input),
        .ram_wr(ram_write), .doc_wr(doc_write), .doc_host_en(doc_host_en)
    );

    // The production RAM port serves the oscillator on the slot edge.
    syncram ram(.clk(clk), .we(ram_write), .data_in(sound_input),
                .addr(ram_addr), .data_out(ram_output));
    always @(posedge clk) begin
        if (doc_write && doc_host_en)
            doc_regs[host_ram_addr[7:0]] <= sound_input;
        doc_output <= doc_regs[host_ram_addr[7:0]];
    end

    task automatic host_access(input bit writing, input logic [1:0] address,
                               input logic [7:0] value = 0);
        select = 1;
        write = writing;
        host_addr = address;
        host_input = value;
        @(negedge clk);
        select = 0;
    endtask

    task automatic set_address(input logic [15:0] address);
        host_access(1, ADDR_HIGH, address[15:8]);
        host_access(1, ADDR_LOW, address[7:0]);
    endtask

    task automatic finish_slot;
        slot = 1;
        @(negedge clk);
        slot = 0;
        repeat (3) @(negedge clk);
        if (busy)
            $fatal(1, "Sound GLU kept a completed access busy");
    endtask

    task automatic expect_pointer(input logic [15:0] expected);
        host_access(0, ADDR_LOW);
        if (host_output !== expected[7:0] || peek !== expected[7:0])
            $fatal(1, "Sound GLU pointer low expected %02h, got %02h/%02h",
                   expected[7:0], host_output, peek);
        host_access(0, ADDR_HIGH);
        if (host_output !== expected[15:8] || peek !== expected[15:8])
            $fatal(1, "Sound GLU pointer high expected %02h, got %02h/%02h",
                   expected[15:8], host_output, peek);
    endtask

    initial begin
        repeat (4) @(negedge clk);
        reset = 0;
        ram.ram[16'h12fe] = 8'h11;
        ram.ram[16'h12ff] = 8'h22;
        ram.ram[16'h1300] = 8'h33;
        ram.ram[16'h8844] = 8'hb6;
        ram.ram[16'h1440] = 8'h44;
        ram.ram[16'h2440] = 8'h24;
        ram.ram[16'h2441] = 8'h41;
        doc_regs[8'h40] = 8'h07;

        host_access(1, CONTROL, 8'h63);
        host_access(0, CONTROL);
        if (host_output !== 8'h7f || peek !== 8'h7f)
            $fatal(1, "Sound GLU exposed the write-only volume DAC when idle");
        set_address(16'h12fe);
        host_access(0, DATA);
        if (!busy)
            $fatal(1, "Sound GLU returned early, before the next slot");
        host_access(0, CONTROL);
        if (host_output !== 8'hff || peek !== 8'hff)
            $fatal(1, "Sound GLU returned incorrect control bits while busy");
        // This second read does not add another operation or pointer step.
        host_access(0, DATA);
        finish_slot();
        expect_pointer(16'h12ff);
        host_access(0, DATA);
        if (host_output !== 8'h11)
            $fatal(1, "Sound GLU RAM read returned the oscillator byte or wrong host address");
        finish_slot();
        expect_pointer(16'h1300);
        host_access(0, DATA);
        if (host_output !== 8'h22)
            $fatal(1, "Sound GLU RAM read missed the address just before the carry");
        finish_slot();

        // A write can replace a queued read at the same address. Preserve
        // the previous byte and count the surviving operation once.
        host_access(0, DATA);
        host_access(1, DATA, 8'ha5);
        host_access(0, DATA);
        if (host_output !== 8'h33)
            $fatal(1, "Sound GLU pending write changed the returned read latch");
        finish_slot();
        if (peek !== 8'h33 || ram.ram[16'h1301] !== 8'ha5)
            $fatal(1, "Sound GLU did not preserve the write and previous read");
        expect_pointer(16'h1302);

        // The high pointer changes before the service edge. The CPU and
        // oscillator share one RAM port, so the host fetch must wait.
        set_address(16'h1440);
        host_access(0, DATA);
        host_access(1, ADDR_HIGH, 8'h24);
        finish_slot();
        host_access(0, DATA);
        if (host_output !== 8'h24)
            $fatal(1, "Sound GLU pending RAM read did not follow the new high address");
        finish_slot();

        // A change of mode during the RAM read's physical completion
        // cannot alter the data source selected for that read.
        set_address(16'h2441);
        host_access(0, DATA);
        slot = 1;
        @(negedge clk);
        slot = 0;
        host_access(1, CONTROL, 8'h00);
        @(negedge clk);
        host_access(0, DATA);
        if (host_output !== 8'h41)
            $fatal(1, "Sound GLU mode switch replaced an issued RAM read with DOC data");
        finish_slot();

        // A mode change on the write slot suppresses the transition write.
        host_access(1, CONTROL, 8'h40);
        set_address(16'h2440);
        host_access(1, DATA, 8'hb6);
        slot = 1;
        host_access(1, CONTROL, 8'h00);
        slot = 0;
        repeat (3) @(negedge clk);
        if (busy || ram.ram[16'h2440] !== 8'h24 || doc_regs[8'h40] !== 8'h07)
            $fatal(1, "Sound GLU mode switch coincident with a write committed the write");

        // A control transition on the next edge also blocks the write,
        // because that is when the selected RAM or DOC consumes its strobe.
        host_access(1, CONTROL, 8'h60);
        set_address(16'h2440);
        host_access(1, DATA, 8'hb6);
        slot = 1;
        @(negedge clk);
        slot = 0;
        host_access(1, CONTROL, 8'h20);
        if (busy || ram.ram[16'h2440] !== 8'h24 || doc_regs[8'h40] !== 8'h07)
            $fatal(1, "Sound GLU mode switch on the RAM write edge committed the write");
        expect_pointer(16'h2441);

        set_address(16'h0040);
        host_access(1, DATA, 8'hb6);
        slot = 1;
        @(negedge clk);
        slot = 0;
        host_access(1, CONTROL, 8'h60);
        if (busy || doc_regs[8'h40] !== 8'h07)
            $fatal(1, "Sound GLU mode switch on the DOC write edge committed the write");
        expect_pointer(16'h0041);

        // Both payload and address stay fixed during the one-edge DOC
        // write pulse while the CPU can already queue the next request.
        host_access(1, CONTROL, 8'h20);
        set_address(16'h0040);
        host_access(1, DATA, 8'h12);
        slot = 1;
        @(negedge clk);
        slot = 0;
        host_access(1, DATA, 8'h34);
        if (doc_regs[8'h40] !== 8'h12 || !busy)
            $fatal(1, "Sound GLU active DOC write was overwritten by the next host write");
        finish_slot();
        if (doc_regs[8'h41] !== 8'h34)
            $fatal(1, "Sound GLU next DOC write went to the old address");
        expect_pointer(16'h0042);

        $display("PASS: Sound GLU pending priority and scheduled RAM/DOC accesses");
        $finish;
    end

    initial begin
        #100000;
        $fatal(1, "Sound GLU regression timed out");
    end
endmodule

// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ns
module tb_apple_iigs_sound_phase;
    localparam logic [1:0] CONTROL = 0, DATA = 1, ADDRESS_LOW = 2, ADDRESS_HIGH = 3;
    logic clk = 0;
    logic clk_7m_en = 0;
    always #5 clk = ~clk;
    always @(posedge clk) clk_7m_en <= ~clk_7m_en;

    logic reset = 1;
    logic select = 0;
    logic wr = 0;
    logic [1:0] host_addr = 0;
    logic [7:0] host_data = 0;
    wire [7:0] read_rom01, read_rom03;
    wire busy_rom01, busy_rom03;

    sound rom01(
        .CLK_14M(clk), .clk_7M_en(clk_7m_en), .invert_local_7m(1'b0),
        .ph0_en(select), .select(select), .reset(reset), .wr(wr), .host_addr(host_addr),
        .host_data_in(host_data), .host_data_out(read_rom01), .host_read_peek(),
        .host_busy(busy_rom01), .sound_out_l(), .sound_out_r(), .ca(), .irq(),
        .speaker_state(1'b0)
    );
    sound rom03(
        .CLK_14M(clk), .clk_7M_en(clk_7m_en), .invert_local_7m(1'b1),
        .ph0_en(select), .select(select), .reset(reset), .wr(wr), .host_addr(host_addr),
        .host_data_in(host_data), .host_data_out(read_rom03), .host_read_peek(),
        .host_busy(busy_rom03), .sound_out_l(), .sound_out_r(), .ca(), .irq(),
        .speaker_state(1'b0)
    );

    logic previous_doc_slot = 0;
    int ticks_since_doc = 0;
    int slots = 0;
    always @(negedge clk) if (!reset) begin
        if (rom03.osc_en !== previous_doc_slot)
            $fatal(1, "Expected one 14M tick between ROM 01 and ROM 03 sound slots");
        if (rom01.glu.osc_en !== rom01.osc_en || rom03.glu.osc_en !== rom03.osc_en)
            $fatal(1, "The GLU and DOC must share their sound slot in each profile");
        ticks_since_doc++;
        if (rom01.osc_en) begin
            if (slots != 0 && ticks_since_doc != 16)
                $fatal(1, "DOC slots must stay 16 ticks apart");
            ticks_since_doc = 0;
            slots++;
        end
        previous_doc_slot = rom01.osc_en;
    end

    task automatic access(input logic write, input logic [1:0] address,
                          input logic [7:0] value);
        select = 1;
        wr = write;
        host_addr = address;
        host_data = value;
        @(negedge clk);
        select = 0;
    endtask

    task automatic idle();
        while (busy_rom01 || busy_rom03)
            @(negedge clk);
    endtask

    task automatic expect_data(input logic [7:0] expected_rom01,
                               input logic [7:0] expected_rom03);
        access(0, DATA, 0);
        idle();
        access(0, DATA, 0);
        if ({read_rom01, read_rom03} !== {expected_rom01, expected_rom03})
            $fatal(1, "Readback ROM 01/03 expected %02h/%02h, got %02h/%02h",
                   expected_rom01, expected_rom03, read_rom01, read_rom03);
        idle();
    endtask

    initial begin
        repeat (8) @(negedge clk);
        #1 reset = 0;
        @(negedge clk);
        access(1, CONTROL, 8'h60);
        access(1, ADDRESS_HIGH, 8'h00);
        access(1, ADDRESS_LOW, 8'h12);
        access(1, DATA, 8'h77);
        idle();
        access(0, ADDRESS_LOW, 0);
        if ({read_rom01, read_rom03} !== 16'h1313)
            $fatal(1, "Each GLU must increment its pointer once per transfer");

        access(1, CONTROL, 8'h00);
        access(1, ADDRESS_LOW, 8'h12);
        access(1, DATA, 8'h35);
        idle();
        expect_data(8'h35, 8'h35);
        access(1, CONTROL, 8'h40);
        expect_data(8'h77, 8'h77);

        // Native PHA to C03D/C03C writes B6 then 00 on consecutive
        // nominal slow bus cycles. Put the mode change two ticks after
        // the next ROM 01 sound slot, on the ROM 03 GLU's RAM write edge.
        do @(negedge clk); while (!rom01.osc_en);
        repeat (4) @(negedge clk);
        access(1, DATA, 8'hb6);
        repeat (13) @(negedge clk);
        access(1, CONTROL, 8'h00);
        idle();
        expect_data(8'h35, 8'h35);
        access(1, CONTROL, 8'h40);
        expect_data(8'hb6, 8'h77);

        $display("PASS: Sound GLU ROM 01/03 phase, DOC cadence, transfer and mode collision");
        $finish;
    end

    initial begin
        #10000;
        $fatal(1, "Sound GLU phase regression timed out");
    end
endmodule

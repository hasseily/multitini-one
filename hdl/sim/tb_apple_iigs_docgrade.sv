// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ns
module tb_apple_iigs_docgrade;
    localparam logic [1:0] SOUND_CONTROL = 0;
    localparam logic [1:0] SOUND_DATA = 1;
    localparam logic [1:0] SOUND_ADDRESS_LOW = 2;
    localparam logic [1:0] SOUND_ADDRESS_HIGH = 3;

    logic clk = 0;
    logic reset = 1;
    logic clk_7m_en = 0;
    always #5 clk = ~clk;
    always @(posedge clk) clk_7m_en <= ~clk_7m_en;

    logic host_select = 0;
    logic host_write = 0;
    logic [1:0] host_address = 0;
    logic [7:0] host_data = 0;
    wire [7:0] host_read, host_peek, doc_data_in, doc_data_out;
    wire [15:0] register_address;
    wire [16:0] sample_address;
    wire host_busy, doc_write, doc_host_en, osc_en;

    soundglu glu(
        .CLK_14M(clk), .clk_7M_en(clk_7m_en), .ph0_en(host_select),
        .reset(reset), .select(host_select), .wr(host_write), .host_addr(host_address),
        .host_data_in(host_data), .sound_data_in(doc_data_out), .osc_en(osc_en),
        .ram_access(), .host_data_out(host_read), .host_read_peek(host_peek), .host_busy(host_busy),
        .sound_addr(register_address), .sound_data_out(doc_data_in),
        .ram_wr(), .doc_wr(doc_write), .doc_host_en(doc_host_en)
    );

    logic terminate_swap = 0;
    logic [7:0] sample_data = 8'h80;
    always @(posedge clk) begin
        case (sample_address[15:8])
            8'h10: sample_data <= terminate_swap ? 8'h00 : 8'h32;
            8'h20: sample_data <= 8'h75;
            8'h30: sample_data <= 8'h00;
            8'h40: sample_data <= sample_address[7:0] == 0 ? 8'h01 : sample_address[7:0];
            8'h50: case (sample_address[7:0])
                0: sample_data <= 8'h32;
                1: sample_data <= 8'h7f;
                default: sample_data <= 8'h00;
            endcase
            default: sample_data <= 8'h80;
        endcase
    end

    es5503 doc(
        .CLK_14M(clk), .clk_7M_en(clk_7m_en), .reset(reset), .wr(doc_write),
        .host_en(doc_host_en), .reg_addr(register_address[7:0]),
        .reg_data_in(doc_data_in), .sample_data_in(sample_data),
        .data_out(doc_data_out), .addr_out(sample_address),
        .sound_out(), .ca(), .irq(), .osc_en(osc_en)
    );

    // Start on a falling edge and allow the scheduled DOC slot to complete.
    task automatic host_access(input bit write, input logic [1:0] address,
                               input logic [7:0] value);
        host_select = 1;
        host_write = write;
        host_address = address;
        host_data = value;
        @(negedge clk);
        host_select = 0;
        while (host_busy)
            @(negedge clk);
    endtask

    task automatic write_doc(input logic [7:0] address, input logic [7:0] value);
        host_access(1, SOUND_ADDRESS_LOW, address);
        host_access(1, SOUND_DATA, value);
    endtask

    task automatic expect_doc(input logic [7:0] address, input logic [7:0] expected,
                              input string name);
        host_access(1, SOUND_ADDRESS_LOW, address);
        host_access(0, SOUND_DATA, 0);
        host_access(0, SOUND_DATA, 0);
        if (host_read !== expected)
            $fatal(1, "%s: DOC register %02h expected %02h, got %02h",
                   name, address, expected, host_read);
    endtask

    logic [23:0] prior_phase;
    logic [7:0] last_data;
    initial begin
        repeat (8) @(negedge clk);
        reset = 0;
        host_access(1, SOUND_CONTROL, 0);
        host_access(1, SOUND_ADDRESS_HIGH, 0);

        // G3: address changes and a write leave the previous read in the latch.
        write_doc(8'h45, 8'h11);
        write_doc(8'h46, 8'h22);
        write_doc(8'h47, 8'h00);
        expect_doc(8'h45, 8'h11, "G3 fixture");
        host_access(1, SOUND_ADDRESS_LOW, 8'h46);
        host_access(0, SOUND_DATA, 0);
        if (host_read !== 8'h11)
            $fatal(1, "G3: first read after address change expected 11, got %02h", host_read);
        host_access(0, SOUND_DATA, 0);
        if (host_read !== 8'h22)
            $fatal(1, "G3: second read after address change expected 22, got %02h", host_read);
        host_access(1, SOUND_ADDRESS_LOW, 8'h45);
        host_access(0, SOUND_DATA, 0);
        write_doc(8'h47, 8'h33);
        if (host_peek !== 8'h11)
            $fatal(1, "G3: peek after write expected 11, got %02h", host_peek);
        host_access(0, SOUND_DATA, 0);
        if (host_read !== 8'h11)
            $fatal(1, "G3: first read after write expected 11, got %02h", host_read);
        host_access(0, SOUND_DATA, 0);
        if (host_read !== 8'h33)
            $fatal(1, "G3: second read after write expected 33, got %02h", host_read);

        // X2: the unused oscillator count and wavetable bits read as ones.
        write_doc(8'he1, 8'hff);
        expect_doc(8'he1, 8'hff, "X2 maximum oscillator count");
        write_doc(8'he1, 8'h02);
        expect_doc(8'he1, 8'hc3, "X2 two oscillators");
        write_doc(8'hc0, 8'h03);
        expect_doc(8'hc0, 8'h83, "X2 wavetable configuration");

        // D7: with oscillator 0 halted, DATA follows its table and retains a
        // nonzero sample when the next table contains a zero.
        write_doc(8'ha0, 8'h01);
        write_doc(8'ha1, 8'h01);
        write_doc(8'hc0, 8'h00);
        write_doc(8'hc1, 8'h00);
        write_doc(8'h80, 8'h10);
        repeat (640) @(negedge clk);
        expect_doc(8'h60, 8'h32, "D7 original halted table");
        write_doc(8'h80, 8'h20);
        repeat (150) @(negedge clk);
        expect_doc(8'h60, 8'h75, "D7 redirected halted table");
        write_doc(8'h80, 8'h30);
        repeat (150) @(negedge clk);
        expect_doc(8'h60, 8'h75, "D7 halted zero sample");

        // M13: a zero ends oscillator 0 in swap mode while its partner runs.
        // Capture the partner phase immediately before and after the swap.
        write_doc(8'h80, 8'h10);
        write_doc(8'h81, 8'h20);
        write_doc(8'h00, 8'h00);
        write_doc(8'h20, 8'h00);
        write_doc(8'h01, 8'h00);
        write_doc(8'h21, 8'h04);
        write_doc(8'ha0, 8'h06);
        write_doc(8'ha1, 8'h00);
        repeat (180) @(negedge clk);
        if (doc.r_control[1][0] !== 1'b0 || doc.accumulator[1] == 0)
            $fatal(1, "M13 fixture: swap partner must already be running with a nonzero phase");
        terminate_swap = 1;
        do @(negedge clk);
        while (!osc_en || doc.current_osc != 0 || doc.refreshing || !doc.sample_zero[0]);
        prior_phase = doc.accumulator[1];
        @(negedge clk);
        if (doc.r_control[0][0] !== 1'b1 || doc.r_control[1][0] !== 1'b0)
            $fatal(1, "M13: expected oscillator 0 to halt and oscillator 1 to keep running");
        if (doc.accumulator[1] !== prior_phase)
            $fatal(1, "M13: running swap partner phase expected %06h, got %06h",
                   prior_phase, doc.accumulator[1]);

        // M7: oscillator 0 wraps in sync mode and resets its higher odd mate.
        terminate_swap = 0;
        write_doc(8'h00, 8'hff);
        write_doc(8'h20, 8'hff);
        write_doc(8'ha0, 8'h04);
        do @(negedge clk);
        while (!osc_en || doc.current_osc != 0 || doc.refreshing ||
               !doc.accumulator_wrapped || doc.r_control[0][0]);
        prior_phase = doc.accumulator[1];
        @(negedge clk);
        if (prior_phase == 0 || doc.accumulator[1] !== 0)
            $fatal(1, "M7: expected even sync wrap to reset odd mate phase from %06h, got %06h",
                   prior_phase, doc.accumulator[1]);

        // D1: stop scanning oscillator 1, halt it, then restore the scan.
        // Its first halted visit still fetches the next in-flight sample.
        write_doc(8'ha0, 8'h01);
        write_doc(8'ha1, 8'h01);
        write_doc(8'h81, 8'h40);
        write_doc(8'h21, 8'h02);
        write_doc(8'ha1, 8'h00);
        repeat (640) @(negedge clk);
        write_doc(8'he1, 8'h00);
        repeat (640) @(negedge clk);
        host_access(1, SOUND_ADDRESS_LOW, 8'h61);
        host_access(0, SOUND_DATA, 0);
        last_data = host_peek;
        if (last_data < 2 || last_data > 8'hfd)
            $fatal(1, "D1 fixture: expected rising ramp during the scan, got %02h", last_data);
        write_doc(8'ha1, 8'h01);
        write_doc(8'he1, 8'h02);
        repeat (160) @(negedge clk);
        expect_doc(8'h61, last_data + 8'd1, "D1 final halted fetch");

        // D6: the zero at the end of a free-run must halt playback without
        // replacing its last nonzero DATA byte with zero or the table base.
        write_doc(8'h81, 8'h50);
        write_doc(8'ha1, 8'h00);
        repeat (640) @(negedge clk);
        expect_doc(8'ha1, 8'h01, "D6 zero halt");
        expect_doc(8'h61, 8'h7f, "D6 last nonzero byte");

        // M2: a halted odd swap oscillator keeps the even one-shot running.
        write_doc(8'h80, 8'h40);
        write_doc(8'h81, 8'h20);
        write_doc(8'h00, 8'hff);
        write_doc(8'h20, 8'hff);
        write_doc(8'ha1, 8'h07);
        write_doc(8'ha0, 8'h02);
        repeat (1200) @(negedge clk);
        expect_doc(8'ha0, 8'h02, "M2 one-shot paired with odd halted swap");
        expect_doc(8'ha1, 8'h07, "M2 odd swap stays halted");

        // M3: a CPU-halted even swap with a nonzero sample does not start
        // its mate. M4: a zero halt in even swap does restart its odd mate.
        write_doc(8'ha0, 8'h01);
        write_doc(8'ha1, 8'h01);
        write_doc(8'h80, 8'h20);
        write_doc(8'ha0, 8'h07);
        repeat (200) @(negedge clk);
        expect_doc(8'ha1, 8'h01, "M3 explicit even swap halt");
        write_doc(8'h80, 8'h30);
        write_doc(8'ha0, 8'h06);
        repeat (200) @(negedge clk);
        expect_doc(8'ha0, 8'h07, "M4 even zero halt");
        expect_doc(8'ha1, 8'h00, "M4 first mate start");
        write_doc(8'ha1, 8'h01);
        repeat (200) @(negedge clk);
        expect_doc(8'ha1, 8'h00, "M4 repeat mate start");

        $display("PASS: DOCGRADE G3, X2, D1, D6, D7, M2, M3, M4, M7, and M13 behavior through the sound GLU");
        $finish;
    end

    initial begin
        #250000;
        $fatal(1, "DOCGRADE regression timed out");
    end
endmodule

// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ns
module tb_apple_iigs_doc_oir;
    logic clk = 0;
    logic reset = 1;
    logic clk_7m_en = 0;
    always #5 clk = ~clk;
    always @(posedge clk) clk_7m_en <= ~clk_7m_en;

    logic host_select = 0;
    logic host_write = 0;
    logic [1:0] host_address = 0;
    logic [7:0] host_data = 0;
    wire [7:0] host_peek, doc_data_in, doc_data_out;
    wire [15:0] register_address;
    wire [16:0] sample_address;
    wire host_busy, doc_write, doc_host_en, osc_en, irq;

    soundglu glu(
        .CLK_14M(clk), .clk_7M_en(clk_7m_en), .ph0_en(host_select),
        .reset(reset), .select(host_select), .wr(host_write), .host_addr(host_address),
        .host_data_in(host_data), .sound_data_in(doc_data_out), .osc_en(osc_en),
        .ram_access(), .host_data_out(), .host_read_peek(host_peek), .host_busy(host_busy),
        .sound_addr(register_address), .sound_data_out(doc_data_in),
        .ram_wr(), .doc_wr(doc_write), .doc_host_en(doc_host_en)
    );

    // One page per oscillator with a nonzero sample or a zero terminator.
    // Preserve the synchronous read latency of sound RAM.
    logic [31:0] terminators = 0;
    logic [7:0] sample_data = 8'h80;
    always @(posedge clk)
        sample_data <= terminators[sample_address[12:8]] ? 8'h00 : 8'h80;

    es5503 doc(
        .CLK_14M(clk), .clk_7M_en(clk_7m_en), .reset(reset), .wr(doc_write),
        .host_en(doc_host_en), .reg_addr(register_address[7:0]), .reg_data_in(doc_data_in),
        .sample_data_in(sample_data), .data_out(doc_data_out), .addr_out(sample_address),
        .sound_out(), .ca(), .irq(irq), .osc_en(osc_en)
    );

    // Call at a falling edge. The GLU returns the prefetched byte through
    // its peek path once the data register access has completed.
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

    task automatic doc_reg_write(input logic [7:0] address, input logic [7:0] value);
        host_access(1, 2, address);
        host_access(1, 1, value);
    endtask

    task automatic read_oir(input logic [7:0] expected, input bit irq_after);
        host_access(0, 1, 0);
        if (host_peek !== expected || irq !== irq_after)
            $fatal(1, "OIR byte/IRQ expected %02h/%b, got %02h/%b",
                   expected, irq_after, host_peek, irq);
    endtask

    task automatic start_osc(input logic [4:0] oscillator);
        doc_reg_write(8'ha0 + {3'b000, oscillator}, 8'h0a);
    endtask

    task automatic terminate_next_scan(input logic [31:0] mask);
        while (doc.refreshing || doc.current_osc != 0)
            @(negedge clk);
        terminators = mask;
    endtask

    initial begin
        repeat (6) @(negedge clk);
        reset = 0;
        host_access(1, 0, 0);
        host_access(1, 3, 0);
        for (int i = 0; i < 32; i++) begin
            doc_reg_write(8'ha0 + 8'(i), 8'h01);
            doc_reg_write(8'h00 + 8'(i), 0);
            doc_reg_write(8'h20 + 8'(i), 0);
            doc_reg_write(8'h80 + 8'(i), 8'(i));
            doc_reg_write(8'hc0 + 8'(i), 0);
        end
        doc_reg_write(8'he1, 8'h08);
        host_access(1, 2, 8'he0);
        read_oir(8'hff, 0);

        // Four one-shots finish in the same scan. The ROM 03 OIR reports
        // each oscillator and retains the last one with bit 7 set.
        for (int i = 1; i <= 4; i++)
            start_osc(5'(i));
        terminate_next_scan(32'h1e);
        repeat (300) @(negedge clk);
        host_access(1, 2, 8'he0);
        read_oir(8'h43, 1);
        read_oir(8'h45, 1);
        read_oir(8'h47, 1);
        read_oir(8'h49, 0);
        read_oir(8'hc9, 0);
        read_oir(8'hc9, 0);

        // Make oscillator 4 finish earlier than 1, 2 and 3.
        terminators = 0;
        for (int i = 1; i <= 4; i++)
            start_osc(5'(i));
        terminators = 32'h10;
        repeat (300) @(negedge clk);
        terminate_next_scan(32'h1e);
        repeat (300) @(negedge clk);
        host_access(1, 2, 8'he0);
        read_oir(8'h49, 1);
        read_oir(8'h43, 1);
        read_oir(8'h45, 1);
        read_oir(8'h47, 0);
        read_oir(8'hc7, 0);

        // On this capture edge, the GLU sees empty while oscillator 1
        // simultaneously queues an IRQ. The following read must see it.
        terminators = 0;
        start_osc(1);
        host_access(1, 2, 8'he0);
        terminators = 32'h02;
        do @(negedge clk);
        while (doc.current_osc != 5'd1 || doc.clk_phase != 3'd7 ||
               clk_7m_en || doc.r_sample_data[1] != 8'h00);
        if (irq)
            $fatal(1, "Coincident DOC interrupt arrived before the GLU read");
        read_oir(8'hc7, 1);
        read_oir(8'h43, 0);
        read_oir(8'hc3, 0);

        // All 32 oscillators can be pending. A full queue wraps its write
        // pointer to its read pointer, but the pending flags keep IRQ live.
        terminators = 0;
        doc_reg_write(8'he1, 8'h3e);
        for (int i = 0; i < 32; i++)
            start_osc(5'(i));
        terminate_next_scan(32'hffffffff);
        repeat (700) @(negedge clk);
        host_access(1, 2, 8'he0);
        for (int i = 0; i < 32; i++)
            read_oir(8'h41 + 8'(2 * i), i != 31);
        read_oir(8'hff, 0);

        $display("PASS: DOC OIR reports oldest interrupt through the GLU, including concurrent enqueue and pointer wrap");
        $finish;
    end

    initial begin
        #1000000;
        $fatal(1, "DOC OIR test timed out");
    end
endmodule

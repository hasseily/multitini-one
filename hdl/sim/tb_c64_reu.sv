// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps
module tb_c64_reu #(
    parameter integer ADDRESS_BITS = 19
);
    logic clk = 0;
    always #5 clk = !clk;
    logic reset = 1, enable = 1;
    logic io_valid = 0, io_write = 0, ff00_write = 0;
    logic [4:0] io_addr = 0;
    logic [7:0] io_wdata = 0, io_rdata;
    wire dma_req, dma_valid, dma_write, dma_ack, dma_idle, dma_peek, dma_last_write;
    wire [15:0] dma_addr;
    wire [7:0] dma_wdata;
    logic [7:0] dma_rdata;
    wire [23:0] memory_addr;
    wire [23:0] memory_hint_addr;
    wire memory_hint_read;
    wire memory_write, memory_valid, memory_ready, memory_rvalid;
    wire [63:0] memory_wdata;
    logic [63:0] memory_rdata;
    wire [7:0] memory_wstrb;
    logic memory_fault = 0;
    wire irq, busy, fault;
    multitini_c64_reu #(.ADDRESS_BITS(ADDRESS_BITS)) dut (.*);

    byte unsigned c64_mem [0:65535];
    byte unsigned reu_mem [0:(1 << ADDRESS_BITS)-1];
    integer c64_epoch [0:65535];
    integer reu_epoch [0:(1 << ADDRESS_BITS)-1];
    integer epoch = 1, clocks = 0, trace_fd, stimulus_fd;
    integer latency_mode = 1;
    logic backend_pending = 0;
    integer delay_left = 0;
    logic [63:0] backend_data = 0;
    logic inhibit_dma = 0;
    integer accepted_c64 = 0, accepted_memory = 0, stability_checks = 0;
    logic prior_dma_hold = 0, prior_memory_hold = 0;
    logic [24:0] prior_dma_bus;
    logic [96:0] prior_memory_bus;
    function automatic [7:0] c64_read(input integer addr);
        if (c64_epoch[addr] === epoch) c64_read = c64_mem[addr];
        else c64_read = addr ^ (addr >> 8) ^ 'h5a;
    endfunction
    function automatic [7:0] reu_read(input integer addr);
        if (reu_epoch[addr] === epoch) reu_read = reu_mem[addr];
        else reu_read = addr ^ (addr >> 8) ^ (addr >> 16) ^ 'ha5;
    endfunction
    function automatic [63:0] read_line(input integer addr);
        integer i;
        for (i = 0; i < 8; i = i+1) read_line[i*8 +: 8] = reu_read(addr+i);
    endfunction
    assign dma_ack = dma_valid && !inhibit_dma && clocks % 7 == 3;
    assign memory_ready = !backend_pending && clocks % 5 != 1;
    assign memory_rvalid = (backend_pending && delay_left == 0) ||
        (latency_mode == 0 && memory_valid && memory_ready);
    // Refresh at the opposite edge. Continuous calls of functions that read
    // memory arrays do not consistently re-evaluate on array writes in Icarus.
    always @(negedge clk) begin
        dma_rdata = c64_read(dma_addr);
        memory_rdata = backend_pending ? backend_data : read_line(memory_addr);
    end

    always @(posedge clk) begin : backend
        integer lane, addr, lanes;
        clocks <= clocks + 1;
        if (!reset && enable) begin
            if (prior_dma_hold) begin
                if (!dma_valid || {dma_write,dma_addr,dma_wdata} !== prior_dma_bus)
                    $fatal(1, "DMA payload changed before acknowledgement");
                stability_checks = stability_checks + 1;
            end
            if (prior_memory_hold) begin
                if (!memory_valid || {memory_write,memory_addr,memory_wdata,memory_wstrb} !== prior_memory_bus)
                    $fatal(1, "PSRAM payload changed under request backpressure");
                stability_checks = stability_checks + 1;
            end
        end
        prior_dma_hold <= dma_valid && !dma_ack && !reset && enable;
        prior_dma_bus <= {dma_write,dma_addr,dma_wdata};
        prior_memory_hold <= memory_valid && !memory_ready && !reset && enable;
        prior_memory_bus <= {memory_write,memory_addr,memory_wdata,memory_wstrb};
        if (dma_ack && !dma_idle) begin
            if (!dma_req) $fatal(1, "DMA transaction without ownership");
            accepted_c64 = accepted_c64 + 1;
            $fwrite(trace_fd,"C %s %06x %02x\n", dma_write ? "W" : "R", {8'd0,dma_addr}, dma_write ? dma_wdata : dma_rdata);
            if (dma_write) begin c64_mem[dma_addr] = dma_wdata; c64_epoch[dma_addr] = epoch; end
        end
        if (backend_pending) begin
            if (delay_left == 0) backend_pending <= 0;
            else delay_left <= delay_left - 1;
        end
        if (memory_valid && memory_ready) begin
            accepted_memory = accepted_memory + 1;
            if (memory_addr[2:0] != 0) $fatal(1, "Unaligned memory request");
            if (memory_write) begin
                lanes = 0;
                for (lane = 0; lane < 8; lane = lane+1) if (memory_wstrb[lane]) begin
                    addr = memory_addr + lane; lanes = lanes+1;
                    reu_mem[addr] = memory_wdata[lane*8 +: 8]; reu_epoch[addr] = epoch;
                    $fwrite(trace_fd,"M W %06x %02x\n", addr[23:0], memory_wdata[lane*8 +: 8]);
                end
                if (lanes != 1) $fatal(1, "REU byte write used %0d lanes", lanes);
            end else begin
                addr = dut.reu_address;
                if (memory_wstrb != 0) $fatal(1,"Read request has write strobes");
                $fwrite(trace_fd,"M R %06x %02x\n", addr[23:0], reu_read(addr));
            end
            if (latency_mode != 0) begin
                backend_pending <= 1;
                backend_data <= read_line(memory_addr);
                delay_left <= 2 + clocks % 13;
            end
        end
        if (clocks > 15000000) $fatal(1,"REU simulation timeout");
    end

    task automatic write_reg(input integer addr, input integer data);
        @(negedge clk); io_addr=addr; io_wdata=data; io_write=1; io_valid=1;
        @(negedge clk); io_valid=0; io_write=0;
    endtask
    task automatic read_reg(input integer addr);
        reg [7:0] value;
        @(negedge clk); io_addr=addr; io_write=0; io_valid=1; #1; value=io_rdata;
        @(negedge clk); io_valid=0;
        $fwrite(trace_fd,"R %02x %02x %0x\n", addr[7:0], value, irq);
    endtask
    task automatic drain;
        integer timeout_count;
        timeout_count=0;
        repeat(2) @(negedge clk);
        while (busy) begin
            @(negedge clk); timeout_count=timeout_count+1;
            if (timeout_count>7000000) $fatal(1,"Transfer did not finish");
        end
        if (fault) $fatal(1,"Unexpected REU fault");
    endtask

    initial begin : stimulus
        string stimulus_path, trace_path;
        integer op,a,b,fields;
        if (!$value$plusargs("STIMULI=%s",stimulus_path)) $fatal(1,"Missing STIMULI");
        if (!$value$plusargs("TRACE=%s",trace_path)) $fatal(1,"Missing TRACE");
        stimulus_fd=$fopen(stimulus_path,"r"); trace_fd=$fopen(trace_path,"w");
        if (!stimulus_fd || !trace_fd) $fatal(1,"Cannot open test streams");
        repeat(3) @(negedge clk); reset=0;
        while (!$feof(stimulus_fd)) begin
            fields=$fscanf(stimulus_fd,"%d %h %h\n",op,a,b);
            if (fields != 3) $fatal(1,"Malformed stimulus");
            case (op)
                0: begin
                    if (busy || backend_pending) $fatal(1,"Case reset with unfinished operation");
                    @(negedge clk); reset=1; epoch=epoch+1; latency_mode=b;
                    repeat(2) @(negedge clk); reset=0;
                    $fwrite(trace_fd,"E %0d\n",a);
                end
                1: write_reg(a,b);
                2: read_reg(a);
                3: begin @(negedge clk);ff00_write=1;@(negedge clk);ff00_write=0;end
                4: begin drain(); $fwrite(trace_fd,"S %0x %0x\n",irq,dma_req);end
                5: begin c64_mem[a]=b;c64_epoch[a]=epoch;end
                6: begin reu_mem[a]=b;reu_epoch[a]=epoch;end
                7: $fwrite(trace_fd,"Q %06x %02x\n",a[23:0],reu_read(a));
                default: $fatal(1,"Unknown stimulus %0d",op);
            endcase
        end
        $fclose(stimulus_fd);$fclose(trace_fd);
        if (accepted_c64==0 || accepted_memory==0 || stability_checks==0)
            $fatal(1,"Vacuous REU simulation");
        $display("PASS: C64 REU ADDRESS_BITS=%0d C64=%0d PSRAM=%0d hold_checks=%0d",ADDRESS_BITS,accepted_c64,accepted_memory,stability_checks);
        $finish;
    end
endmodule

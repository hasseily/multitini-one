// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Observation-only wrapper for the pre-UA, pre-island production machine.
// The existing machine bench supplies independently checked DMA scenarios.
module tb_c64_ua_stock_bus;
    tb_c64_reu_machine machine();
    integer trace_file;
    integer cycle = 0;
    reg previous_dma = 0, previous_irq = 0;
    integer io_reads = 0, io_writes = 0, acknowledgements = 0;
    initial begin
        trace_file = $fopen("stock_bus.csv", "w");
        if (!trace_file) $fatal(1, "FAIL: UA stock trace file");
        $fwrite(trace_file, "ntsc,cycle,phase,event,address,data,flags\n");
    end
    always @(posedge machine.clk) begin
        if (!machine.running) begin
            cycle = 0;
            previous_dma = 0;
            previous_irq = 0;
        end else if (!machine.dbg_reset && !machine.paused) begin
            cycle++;
            // The historical dense mode is exercised by the parent bench,
            // but this compatibility reference deliberately captures Off.
            if (machine.variant == 0) begin
                if (machine.cpu_bus_valid &&
                    (machine.cpu_bus_addr[15:8] == 8'hdf ||
                     (machine.cpu_bus_write && machine.cpu_bus_addr == 16'hff00))) begin
                    $fwrite(trace_file, "%0d,%0d,%0d,%s,%04x,%02x,%02x\n",
                        machine.ntsc, cycle, machine.dbg_phase,
                        machine.cpu_bus_write ? "CPU_W" : "CPU_R",
                        machine.cpu_bus_addr,
                        machine.cpu_bus_write ? machine.cpu_bus_wdata : machine.dbg_cpu_di,
                        {6'b0,machine.dbg_rdy,machine.dbg_cpu_owned});
                    if (machine.cpu_bus_addr[15:8] == 8'hdf) begin
                        if (machine.cpu_bus_write) io_writes++;
                        else io_reads++;
                    end
                end
                if (machine.dma_ack) begin
                    $fwrite(trace_file, "%0d,%0d,%0d,%s,%04x,%02x,%02x\n",
                        machine.ntsc, cycle, machine.dbg_phase,
                        machine.dma_we ? "DMA_W" : "DMA_R", machine.dma_addr,
                        machine.dma_we ? machine.dma_data : machine.dma_result,
                        {3'b0,machine.dma_last_write,machine.dma_peek,machine.dma_idle,
                         machine.dbg_reu_ba,machine.dbg_aec});
                    acknowledgements++;
                end
                if (previous_dma != machine.dma_req)
                    $fwrite(trace_file, "%0d,%0d,%0d,DMA_OWN,0000,%02x,00\n",
                        machine.ntsc,cycle,machine.dbg_phase,{7'b0,machine.dma_req});
                if (previous_irq != machine.reu_irq)
                    $fwrite(trace_file, "%0d,%0d,%0d,IRQ,0000,%02x,00\n",
                        machine.ntsc,cycle,machine.dbg_phase,{7'b0,machine.reu_irq});
            end
            previous_dma = machine.dma_req;
            previous_irq = machine.reu_irq;
        end
    end
    final begin
        $fclose(trace_file);
        $display("UA_STOCK_TRACE reads=%0d writes=%0d dma_acks=%0d", io_reads,io_writes,acknowledgements);
    end
endmodule

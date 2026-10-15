// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_nes_memory;
    reg clk=0,reset=1,invalidate=0,master_ce=0,machine_reset=1,chr_is_ram=0;
    reg [24:0] cpu_addr=0;
    reg cpu_read=0,cpu_write=0;
    reg [7:0] cpu_wdata=0;
    wire [7:0] cpu_rdata;
    reg [21:0] ppu_addr=0;
    reg ppu_read=0,ppu_write=0;
    reg [7:0] ppu_wdata=0;
    wire [7:0] ppu_rdata;
    wire stall;
    reg [12:0] save_address=0;
    reg [7:0] save_data=0;
    reg save_write=0;
    wire runtime_write_committed;
    wire [23:0] runtime_write_address;
    reg save_read=0;
    reg [23:0] save_addr=24'h3c0000;
    wire save_ready;
    wire [63:0] save_rdata;
    wire save_rvalid;
    reg [23:0] loader_addr=0;
    reg [63:0] loader_wdata=0;
    reg [7:0] loader_wstrb=0;
    reg loader_valid=0;
    wire loader_ready,loader_response;
    wire [23:0] memory_addr;
    wire memory_write;
    wire [63:0] memory_wdata;
    wire [7:0] memory_wstrb;
    wire memory_valid;
    reg memory_ready=0;
    reg [63:0] memory_rdata=0;
    reg memory_rvalid=0;
    integer requests=0,write_requests=0;
    reg backend_pending=0;
    reg [96:0] held_request;
    integer before_requests;
    integer i;
    always #5 clk=!clk;
    multitini_nes_memory dut(.*);

    // Hold either acceptance or completion independently. An accepted request
    // owns the manual backend until its delayed reply has drained.
    always @(posedge clk) begin
        if(reset)begin requests=0;write_requests=0;backend_pending=0;end
        else begin
            if(memory_valid&&memory_ready)begin
                if(backend_pending)$fatal(1,"overlapping external requests");
                if(memory_addr[2:0]!=0)$fatal(1,"unaligned external request");
                requests=requests+1;
                if(memory_write)write_requests=write_requests+1;
                backend_pending=1;
            end
            if(memory_rvalid)begin
                if(!backend_pending)$fatal(1,"response without accepted request");
                backend_pending=0;
            end
        end
    end
    task tick;
        begin @(posedge clk);#1;end
    endtask
    task accept(input [23:0] address,input write_enable);
        begin
            @(negedge clk);memory_ready=1;#1;
            if(!memory_valid||memory_addr!==address||memory_write!==write_enable)
                $fatal(1,"wrong request got %h write%0b expected %h write%0b",
                       memory_addr,memory_write,address,write_enable);
            tick();
            if(!dut.outstanding||loader_ready)$fatal(1,"request did not reserve backend");
            @(negedge clk);memory_ready=0;
        end
    endtask
    task response(input [63:0] data);
        begin
            @(negedge clk);memory_rdata=data;memory_rvalid=1;
            tick();
            if(dut.outstanding)$fatal(1,"reply did not drain outstanding operation");
            @(negedge clk);memory_rvalid=0;
        end
    endtask
    task wait_clear;
        begin
            wait(save_ready);@(negedge clk);
            if(stall&&machine_reset)$fatal(1,"RAM clear did not release reset-side stall");
        end
    endtask

    initial begin
        repeat(3)@(negedge clk);reset=0;
        wait_clear();machine_reset=0;
        // Local writes happen only on a native edge. CPU, NT and CHR mirrors
        // never reach external memory; only runtime SRAM writes mark dirty.
        cpu_addr=25'h380012;cpu_write=1;cpu_wdata=8'h91;
        tick();
        if(cpu_rdata!==0||runtime_write_committed)$fatal(1,"RAM changed without native CE");
        @(negedge clk);master_ce=1;
        tick();
        if(cpu_rdata!==8'h91||runtime_write_committed)$fatal(1,"CPU RAM native write/dirty routing");
        @(negedge clk);master_ce=0;cpu_write=0;cpu_read=1;cpu_addr=25'h380812;#1;
        if(cpu_rdata!==8'h91||memory_valid||stall)$fatal(1,"CPU RAM mirror escaped local memory");
        @(negedge clk);cpu_read=0;ppu_addr=22'h3a0001;ppu_write=1;ppu_wdata=8'h72;master_ce=1;
        tick();
        @(negedge clk);master_ce=0;ppu_write=0;ppu_read=1;ppu_addr=22'h3a0801;#1;
        if(ppu_rdata!==8'h72||memory_valid||stall)$fatal(1,"nametable RAM mirror");
        chr_is_ram=1;ppu_addr=22'h200123;ppu_write=1;ppu_wdata=8'h4e;master_ce=1;
        tick();
        @(negedge clk);master_ce=0;ppu_write=0;ppu_addr=22'h202123;#1;
        if(ppu_rdata!==8'h4e||memory_valid||stall)$fatal(1,"CHR RAM write/read/mirror");
        @(negedge clk);ppu_read=0;cpu_addr=25'h3c0012;cpu_write=1;cpu_wdata=8'ha5;master_ce=1;
        tick();
        if(cpu_rdata!==8'ha5||!runtime_write_committed||runtime_write_address!==24'h3c0012)
            $fatal(1,"SRAM write did not publish exact dirty address");
        @(negedge clk);master_ce=0;cpu_write=0;
        tick();
        if(runtime_write_committed)$fatal(1,"SRAM dirty pulse repeated without a write");
        for(i=0;i<8;i=i+1)begin
            @(negedge clk);save_write=1;save_address=13'h20+i;save_data=8'h80+i;
            tick();
            if(runtime_write_committed)$fatal(1,"save restore counted as runtime dirty write");
        end
        @(negedge clk);save_write=0;save_read=1;save_addr=24'h3c0023;
        tick();
        if(!save_rvalid||save_rdata!==64'h8786858483828180)$fatal(1,"save snapshot byte order/alignment");
        @(negedge clk);save_read=0;
        tick();
        if(save_rvalid||requests!=0)$fatal(1,"local memory touched external bus or stretched reply");

        // Two independent misses must serialize, preserve all request bits
        // under backpressure, and hold the machine until both lines return.
        @(negedge clk);chr_is_ram=0;cpu_addr=25'h103;cpu_read=1;ppu_addr=22'h200205;ppu_read=1;#1;
        if(!stall||!memory_valid)$fatal(1,"simultaneous misses did not hold machine");
        held_request={memory_addr,memory_write,memory_wstrb,memory_wdata};
        repeat(5)begin
            tick();
            if(!memory_valid||{memory_addr,memory_write,memory_wstrb,memory_wdata}!==held_request)
                $fatal(1,"request changed under external backpressure");
            @(negedge clk);
        end
        accept(24'h100,0);
        repeat(5)begin tick();if(!stall||memory_valid)$fatal(1,"outstanding CPU miss escaped hold");@(negedge clk);end
        response(64'h8877665544332211);
        if(cpu_rdata!==8'h44||!stall)$fatal(1,"CPU line bytes or pending PPU hold");
        accept(24'h200200,0);
        response(64'hf8e7d6c5b4a39281);
        if(ppu_rdata!==8'hd6||stall)$fatal(1,"PPU line bytes or combined miss completion");
        before_requests=requests;
        for(i=0;i<8;i=i+1)begin
            @(negedge clk);cpu_addr=25'h100+i;ppu_addr=22'h200200+i;#1;
            if(cpu_rdata!==(64'h8877665544332211>>(i*8)&8'hff)||
               ppu_rdata!==(64'hf8e7d6c5b4a39281>>(i*8)&8'hff)||stall||memory_valid)
                $fatal(1,"cache hit returned wrong byte lane");
        end
        if(requests!=before_requests)$fatal(1,"cache hits issued external requests");

        // A colliding tag must miss. Discard its accepted pre-invalidate reply
        // even when it returns several cycles after the new cartridge reset.
        @(negedge clk);ppu_read=0;cpu_addr=25'h303;#1;
        if(!stall||!memory_valid)$fatal(1,"ROM tag collision falsely hit");
        accept(24'h300,0);
        @(negedge clk);invalidate=1;machine_reset=1;
        tick();
        @(negedge clk);invalidate=0;
        repeat(4)@(negedge clk);
        response(64'hdeadbeef12345678);
        if(dut.cpu_valid!==0||dut.ppu_valid!==0)$fatal(1,"delayed stale reply refilled cache");
        wait_clear();machine_reset=0;#1;
        if(!memory_valid||!stall)$fatal(1,"old epoch retained a false ROM hit");
        accept(24'h300,0);
        response(64'hf0e0d0c0b0a09080);
        if(cpu_rdata!==8'hb0||stall)$fatal(1,"new cartridge data not refetched");

        // Exercise PPU ownership and the exact invalidate/reply clock edge.
        @(negedge clk);cpu_read=0;ppu_read=1;ppu_addr=22'h200405;
        accept(24'h200400,0);
        @(negedge clk);invalidate=1;machine_reset=1;memory_rvalid=1;memory_rdata=64'h1111111111111111;
        tick();
        if(dut.cpu_valid!==0||dut.ppu_valid!==0||dut.outstanding)
            $fatal(1,"same-edge invalidate/reply refilled or failed to drain");
        @(negedge clk);invalidate=0;memory_rvalid=0;
        wait_clear();machine_reset=0;#1;
        if(!memory_valid||!stall)$fatal(1,"same-edge stale PPU response became a cache hit");
        accept(24'h200400,0);
        response(64'h8070605040302010);
        if(ppu_rdata!==8'h60||stall)$fatal(1,"PPU did not recover after same-edge discard");

        // Ready must describe an actual backend handshake. Invalidate cannot
        // acknowledge a loader write that memory_valid has suppressed.
        @(negedge clk);ppu_read=0;machine_reset=1;invalidate=1;
        loader_valid=1;loader_addr=24'h18;loader_wdata=64'h1020304050607080;loader_wstrb=8'h55;memory_ready=1;#1;
        before_requests=requests;
        if(loader_ready||memory_valid)$fatal(1,"loader accepted during invalidation");
        tick();
        if(dut.outstanding||requests!=before_requests||loader_response)
            $fatal(1,"invalidated loader request reached external memory");
        @(negedge clk);invalidate=0;memory_ready=0;#1;
        held_request={memory_addr,memory_write,memory_wstrb,memory_wdata};
        repeat(4)begin
            tick();
            if(loader_ready||!memory_valid||{memory_addr,memory_write,memory_wstrb,memory_wdata}!==held_request)
                $fatal(1,"loader request changed under backpressure");
            @(negedge clk);
        end
        accept(24'h18,1);
        loader_valid=0;#1;
        if(loader_response||memory_wdata!==64'h1020304050607080||memory_wstrb!==0)
            $fatal(1,"write acknowledged before physical completion");
        response(64'd0);
        if(!loader_response||write_requests!=1)$fatal(1,"loader did not receive exactly one completed write");
        tick();
        if(loader_response)$fatal(1,"loader completion repeated");
        $display("PASS NES memory: local RAM/mirrors, SRAM dirty/snapshot lanes, CPU/PPU misses, stable backpressure, tag collisions, delayed/same-edge invalidation and loader handshake/completion");
        $finish;
    end
    initial begin #1000000;$fatal(1,"NES memory timeout");end
endmodule

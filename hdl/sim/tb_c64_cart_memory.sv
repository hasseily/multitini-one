// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

module tb_c64_cart_memory;
    logic clk = 1'b0;
    logic reset = 1'b1;
    logic loader_active = 1'b0;
    logic [31:0] erase_request = 0;
    logic erase_allowed = 0;
    wire [31:0] erase_status;
    logic [23:0] loader_addr = 24'd0;
    logic loader_write = 1'b0;
    logic [63:0] loader_wdata = 64'd0;
    logic [7:0] loader_wstrb = 8'd0;
    logic loader_valid = 1'b0;
    logic loader_ready;
    logic core_request = 1'b0;
    logic core_write = 1'b0;
    logic [23:0] core_addr = 24'd0;
    logic [7:0] core_wdata = 8'd0;
    logic [7:0] core_rdata;
    logic core_stall;
    logic busy;
    logic loader_busy;
    logic fault;
    logic clear_fault = 1'b0;
    logic [23:0] memory_addr;
    logic memory_write;
    logic [63:0] memory_wdata;
    logic [7:0] memory_wstrb;
    logic memory_valid;
    logic memory_ready = 1'b0;
    logic [63:0] memory_rdata = 64'h7766554433221100;
    logic memory_rvalid = 1'b0;
    logic memory_fault = 1'b0;
    logic flash_valid = 1'b0;
    logic flash_write = 1'b0;
    logic [23:0] flash_addr = 24'd0;
    logic [7:0] flash_wdata = 8'd0;
    logic flash_accept;
    logic flash_done;
    logic [7:0] flash_rdata;
    logic flash_erase_valid = 1'b0;
    logic [2:0] flash_erase_group = 3'd0;
    logic flash_erase_high = 1'b0;
    logic flash_erase_accept;
    logic flash_erase_done;
    integer flash_done_count = 0, flash_erase_done_count = 0;

    always #5 clk = !clk;
    multitini_c64_cart_memory dut (.*);
    always @(posedge clk) begin
        if (flash_done) flash_done_count++;
        if (flash_erase_done) flash_erase_done_count++;
    end
    // Accept and answer the presented request after a short backpressure.
    task automatic serve(input integer delay);
        repeat(delay)@(negedge clk);
        memory_ready=1;@(negedge clk);memory_ready=0;
        memory_rvalid=1;@(negedge clk);memory_rvalid=0;
    endtask

    task automatic check(input logic condition, input string message);
        if (!condition) begin
            $display("FAIL: %s", message);
            $fatal(1);
        end
    endtask

    task automatic erase_sector(input [3:0] token,input [5:0] bank,input bit high_rom);
        integer waited;
        reg [23:0] expected;
        @(negedge clk);
        erase_request={token,1'b1,20'd0,high_rom,bank};
        @(negedge clk);
        check(erase_status[16]&&!erase_status[17],"erase starts while safely paused");
        for(integer word_index=0;word_index<8192;word_index++)begin
            waited=0;
            while(!memory_valid&&waited<16)begin @(negedge clk);waited++;end
            check(waited<16,"next erase write issued");
            expected=24'h200000+((bank&6'h38)+word_index/1024)*16384+
                     (high_rom?8192:0)+(word_index%1024)*8;
            check(memory_write&&memory_addr==expected&&memory_wstrb==8'hff&&
                  memory_wdata==64'hffffffffffffffff,"erase geometry and FF write payload");
            check(!loader_busy,"erase must not assert content-loader reset");
            repeat(word_index%3)begin
                @(negedge clk);
                check(memory_valid&&memory_addr==expected,"erase request stable under backpressure");
            end
            memory_ready=1;@(negedge clk);memory_ready=0;
            check(erase_status[16],"erase waits for write completion");
            repeat(word_index%4)@(negedge clk);
            memory_rvalid=1;@(negedge clk);memory_rvalid=0;
        end
        check(!erase_status[16]&&!erase_status[17]&&erase_status[3:0]==token,
              "completion token follows all 8192 acknowledged writes");
        repeat(8)@(negedge clk);
        check(!memory_valid&&!busy,"held request is not replayed");
    endtask

    initial begin
        repeat (2) @(negedge clk);
        reset = 1'b0;

        loader_active = 1'b1;
        loader_addr = 24'h200008;
        loader_write = 1'b1;
        loader_wdata = 64'h000000000000aa00;
        loader_wstrb = 8'h02;
        loader_valid = 1'b1;
        @(posedge clk); #1;
        check(!loader_ready && memory_valid && memory_write,
              "loader write waits for backend acceptance");
        check(memory_addr == loader_addr && memory_wstrb == 8'h02,
              "loader payload forwarded");
        memory_ready = 1'b1;
        @(negedge clk);
        loader_valid = 1'b0;
        memory_rvalid = 1'b1;
        @(negedge clk);
        memory_rvalid = 1'b0;
        loader_active = 1'b0;
        check(!loader_busy, "loader completion released");

        core_addr = 24'h200003;
        core_request = 1'b1;
        memory_ready = 1'b0;
        @(posedge clk); #1;
        check(core_stall, "cache miss stalls core");
        @(posedge clk); #1;
        check(memory_valid && !memory_write &&
              memory_addr == 24'h200000, "aligned read issued");
        memory_ready = 1'b1;
        @(negedge clk);
        memory_rvalid = 1'b1;
        @(negedge clk);
        memory_rvalid = 1'b0;
        #1;
        check(!core_stall && core_rdata == 8'h33,
              "response fills byte-select cache");

        @(negedge clk);
        core_request = 1'b0;
        @(negedge clk);
        core_addr = 24'h20000e;
        core_wdata = 8'ha5;
        core_write = 1'b1;
        core_request = 1'b1;
        memory_ready = 1'b0;
        @(posedge clk); #1;
        check(core_stall, "external cartridge write stalls core");
        @(posedge clk); #1;
        check(memory_valid && memory_write &&
              memory_addr == 24'h200008 && memory_wstrb == 8'h40 &&
              memory_wdata[55:48] == 8'ha5,
              "external cartridge byte write is aligned and strobed");
        memory_ready = 1'b1;
        @(negedge clk);
        memory_rvalid = 1'b1;
        @(negedge clk);
        memory_rvalid = 1'b0;
        #1;
        check(!core_stall, "write response releases held request");
        core_request = 1'b0;
        core_write = 1'b0;

        memory_fault = 1'b1;
        @(negedge clk);
        memory_fault = 1'b0;
        check(fault, "memory fault is sticky");
        clear_fault = 1'b1;
        @(negedge clk);
        clear_fault = 1'b0;
        check(!fault, "fault clears explicitly");
        memory_ready=0;
        erase_request=32'h08000000; // token zero is a real first request when VALID is set
        @(negedge clk);
        check(erase_status==32'h80020000&&!memory_valid,"unsafe erase rejected without a write");
        erase_allowed=1;
        // Seed a runtime cache line, then prove erase invalidates it.
        core_addr=24'h200003;core_request=1;
        repeat(2)@(negedge clk);memory_ready=1;@(negedge clk);memory_ready=0;
        memory_rvalid=1;@(negedge clk);memory_rvalid=0;core_request=0;
        @(negedge clk);check(core_rdata==8'h33,"runtime cache seeded before erase");
        erase_sector(4'hf,6'h07,0); // low three bank bits are ignored
        core_request=1;repeat(2)@(negedge clk);
        check(core_stall&&memory_valid&&!memory_write,"erase invalidates cached ROM data");
        memory_ready=1;@(negedge clk);memory_ready=0;
        memory_rdata=64'hffffffffffffffff;memory_rvalid=1;@(negedge clk);memory_rvalid=0;
        check(core_rdata==8'hff&&!core_stall,"runtime sees erased contents");core_request=0;
        erase_sector(4'h0,6'h3f,1); // sequence wrap and top high-ROM sector
        @(negedge clk);loader_active=1;erase_request=32'h18000000;@(negedge clk);
        check(erase_status==32'h80020001&&!memory_valid,"loader owns backing memory exclusively");
        loader_active=0;erase_request=32'h28000080;@(negedge clk);
        check(erase_status==32'h80020002&&!memory_valid,"reserved request bits rejected");
        erase_request=32'h38000000;@(negedge clk);check(erase_status[16],"fault test starts");
        memory_ready=1;@(negedge clk);memory_ready=0;
        memory_fault=1;@(negedge clk);memory_fault=0;
        check(erase_status==32'h80020003&&fault&&!memory_valid,"backend fault terminates with error token");

        // EasyFlash byte reads return their lane; byte writes are strobed.
        clear_fault=1;@(negedge clk);clear_fault=0;
        memory_rdata=64'h7766554433221100;
        flash_addr=24'h200025;flash_write=0;flash_valid=1;
        @(posedge clk);#1;
        check(memory_valid&&!memory_write&&memory_addr==24'h200020,"flash read is word aligned");
        @(negedge clk);memory_ready=1;#1;
        check(flash_accept,"flash read accepted");
        @(negedge clk);memory_ready=0;flash_valid=0;
        memory_rvalid=1;@(negedge clk);memory_rvalid=0;@(negedge clk);
        check(flash_done_count==1&&flash_rdata==8'h55,"flash read returns its byte lane");
        // A flash write invalidates the machine's cached copy of its word.
        core_addr=24'h200022;core_request=1;serve(2);core_request=0;
        @(negedge clk);check(core_rdata==8'h22,"machine line cached before flash write");
        flash_addr=24'h200021;flash_wdata=8'h3c;flash_write=1;flash_valid=1;
        @(posedge clk);#1;
        check(memory_valid&&memory_write&&memory_addr==24'h200020&&memory_wstrb==8'h02&&
              memory_wdata[15:8]==8'h3c,"flash write is one strobed byte");
        @(negedge clk);memory_ready=1;@(negedge clk);memory_ready=0;flash_valid=0;flash_write=0;
        memory_rvalid=1;@(negedge clk);memory_rvalid=0;@(negedge clk);
        check(flash_done_count==2,"flash write completes");
        core_request=1;repeat(2)@(negedge clk);
        check(core_stall&&memory_valid&&!memory_write&&memory_addr==24'h200020,
              "flash write leaves no stale cached byte");
        serve(0);core_request=0;
        // The running machine is served before waiting flash traffic.
        @(negedge clk);core_addr=24'h200040;core_request=1;
        flash_addr=24'h200048;flash_valid=1;
        repeat(2)@(negedge clk);
        check(memory_valid&&!memory_write&&memory_addr==24'h200040,"machine read wins over flash");
        serve(0);core_request=0;
        @(posedge clk);#1;check(memory_valid&&memory_addr==24'h200048,"flash read follows");
        @(negedge clk);memory_ready=1;@(negedge clk);memory_ready=0;flash_valid=0;
        memory_rvalid=1;@(negedge clk);memory_rvalid=0;@(negedge clk);
        check(flash_done_count==3,"deferred flash read completes");

        // Flash sector erase runs while the machine keeps its bus priority.
        core_addr=24'h200003;core_request=1;serve(2);core_request=0;
        @(negedge clk);flash_erase_group=3'd0;flash_erase_high=1'b0;flash_erase_valid=1;
        #1;check(flash_erase_accept,"flash erase accepted while running");
        @(negedge clk);flash_erase_valid=0;
        check(!erase_status[16]&&erase_status[3:0]==4'h3,"flash erase does not claim the UCI token");
        for(integer word_index=0;word_index<8192;word_index++)begin
            integer waited;
            if(word_index==5)begin
                // A machine read during the erase issues before the next beat.
                core_addr=24'h208003;core_request=1;
                waited=0;
                while(!(memory_valid&&!memory_write)&&waited<16)begin @(negedge clk);waited++;end
                check(waited<16&&memory_addr==24'h208000,"machine read interleaves with erase");
                serve(0);core_request=0;
                check(core_rdata==8'h33,"machine read during erase returns data");
            end
            waited=0;
            while(!(memory_valid&&memory_write)&&waited<16)begin @(negedge clk);waited++;end
            check(waited<16&&memory_wstrb==8'hff&&memory_addr==24'h200000+
                  (word_index/1024)*16384+(word_index%1024)*8,"flash erase geometry");
            serve(word_index%2);
        end
        repeat(2)@(negedge clk);
        check(flash_erase_done_count==1&&!busy&&erase_status[3:0]==4'h3,
              "flash erase completes without touching the UCI token");
        // New content stops a flash erase.
        flash_erase_group=3'd2;flash_erase_high=1'b1;flash_erase_valid=1;
        @(negedge clk);flash_erase_valid=0;
        @(posedge clk);#1;check(memory_valid&&memory_write,"second flash erase starts");
        serve(0);loader_active=1;repeat(4)@(negedge clk);
        check(!memory_valid&&!busy&&flash_erase_done_count==1,"loader aborts flash erase");
        loader_active=0;
        $display("PASS: C64 cartridge memory bridge and acknowledged EasyFlash sectors");
        $finish;
    end
endmodule

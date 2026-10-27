// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

module tb_c64_content_loader;
    import multitini_c64_defs_pkg::*;

    logic clk = 1'b0;
    logic reset = 1'b1;
    logic load_abort = 1'b0;
    logic load_abort_epoch = 1'b0;
    logic clear_error = 1'b0;
    logic [7:0] load_data = 8'd0;
    logic load_valid = 1'b0;
    logic load_ready;
    logic load_first = 1'b0;
    logic load_last = 1'b0;
    logic [7:0] load_kind = 8'd0;
    logic [31:0] load_offset = 32'd0;
    logic [31:0] load_total = 32'd0;
    logic load_epoch = 1'b0;
    logic [15:0] core_addr = 16'h0000;
    logic prg_ram_wr;
    logic [15:0] prg_ram_addr;
    logic [7:0] prg_ram_data;
    logic prg_loaded;
    logic prg_run_request;
    logic [23:0] loader_memory_addr;
    logic loader_memory_write;
    logic [63:0] loader_memory_wdata;
    logic [7:0] loader_memory_wstrb;
    logic loader_memory_valid;
    logic loader_memory_ready = 1'b1;
    logic memory_fault = 1'b0;
    logic cart_loading;
    logic cart_loaded;
    logic [7:0] cart_id;
    logic [7:0] cart_subtype;
    logic [2:0] cart_expansion_policy;
    logic cart_exrom;
    logic cart_game;
    logic cart_bank_hi;
    logic cart_bank_16k;
    logic [7:0] cart_bank_num;
    logic [7:0] cart_bank_addr;
    logic cart_bank_wr;
    logic core_reset_hold;
    logic prg_install_pause;
    logic active;
    logic error;
    logic [7:0] ram [0:65535];
    // Payload bytes reach memory packed: cart_write_count counts bytes,
    // cart_word_count writes, and cart_partial_count writes of fewer than 8.
    integer cart_write_count = 0;
    integer cart_word_count = 0;
    integer cart_partial_count = 0;
    integer bank_write_count = 0;
    integer run_request_count = 0;
    localparam logic [8*16-1:0] CRT_MAGIC = "C64 CARTRIDGE   ";

    always #5 clk = !clk;
    multitini_c64_content_loader dut (.*);

    always @(posedge clk) begin
        if (prg_ram_wr) ram[prg_ram_addr] <= prg_ram_data;
        if (loader_memory_valid && loader_memory_ready) begin
            cart_write_count <= cart_write_count + $countones(loader_memory_wstrb);
            cart_word_count <= cart_word_count + 1;
            if (loader_memory_wstrb != 8'hff)
                cart_partial_count <= cart_partial_count + 1;
        end
        if (cart_bank_wr) bank_write_count <= bank_write_count + 1;
        if (prg_run_request) run_request_count <= run_request_count + 1;
    end

    task automatic check(input logic condition, input string message);
        if (!condition) begin
            $display("FAIL: %s", message);
            $fatal(1);
        end
    endtask

    task automatic send_byte(
        input logic [7:0] kind,
        input integer offset,
        input integer total,
        input logic [7:0] value
    );
        while (!load_ready) @(negedge clk);
        load_kind = kind;
        load_offset = offset;
        load_total = total;
        load_data = value;
        load_first = offset == 0;
        load_last = offset == total - 1;
        load_valid = 1'b1;
        @(negedge clk);
        load_valid = 1'b0;
        load_first = 1'b0;
        load_last = 1'b0;
    endtask

    task automatic abort_stream(input logic epoch);
        @(negedge clk);
        load_abort_epoch = epoch;
        load_abort = 1'b1;
        @(negedge clk);
        load_abort = 1'b0;
    endtask

    function automatic logic [7:0] crt_byte(input integer offset);
        begin
            crt_byte = 8'd0;
            if (offset < 16)
                crt_byte = CRT_MAGIC[(15-offset)*8 +: 8];
            case (offset)
                19: crt_byte = 8'h40;
                20: crt_byte = 8'h01;
                23: crt_byte = MT_CRT_MAGIC_DESK;
                25: crt_byte = 8'h01;
                64: crt_byte = "C";
                65: crt_byte = "H";
                66: crt_byte = "I";
                67: crt_byte = "P";
                69: crt_byte = 8'h00;
                70: crt_byte = 8'h20;
                71: crt_byte = 8'h10;
                76: crt_byte = 8'h80;
                78: crt_byte = 8'h20;
                default:
                    if (offset >= 80) crt_byte = offset[7:0] ^ 8'ha5;
            endcase
        end
    endfunction

    function automatic logic [7:0] fc3_byte(input integer offset,
        input integer subtype, input integer bad_bank);
        integer chip, local_offset;
        begin
            fc3_byte = 0;
            if (offset < 64) begin
                if (offset < 16) fc3_byte = CRT_MAGIC[(15-offset)*8 +: 8];
                case (offset)
                    19: fc3_byte = 8'h40;
                    20: fc3_byte = 1;
                    23: fc3_byte = MT_CRT_FINAL_CARTRIDGE_3;
                    26: fc3_byte = subtype[7:0];
                    default: ;
                endcase
            end else begin
                chip = (offset - 64) / 16400;
                local_offset = (offset - 64) % 16400;
                case (local_offset)
                    0: fc3_byte = "C"; 1: fc3_byte = "H";
                    2: fc3_byte = "I"; 3: fc3_byte = "P";
                    6: fc3_byte = 8'h40; 7: fc3_byte = 8'h10;
                    11: fc3_byte = bad_bank && chip == 2 ? 1 : chip[7:0];
                    12: fc3_byte = 8'h80; 14: fc3_byte = 8'h40;
                    default: if (local_offset >= 16) fc3_byte = chip[7:0] ^ offset[7:0];
                endcase
            end
        end
    endfunction

    function automatic logic [7:0] tmb_byte(input integer offset,
        input integer variant);
        begin
            tmb_byte = fc3_byte(offset, 0, variant == 3);
            if (offset == 23) tmb_byte = MT_CRT_TWO_MEGABYTER;
            if (offset == 26 && variant == 4) tmb_byte = 1;
            if (offset == 78 && variant == 5) tmb_byte = 8'h20;
            if (offset == 25 && variant == 6) tmb_byte = 1;
            if (offset == 75 && variant == 7) tmb_byte = 128;
        end
    endfunction

    // Canonical ROM-only batch: ordered packets; payload is synthetic.
    function automatic logic [7:0] rom_batch_byte(input integer offset,
        input integer id, size, variant);
        integer packet, local_offset;
        begin
            rom_batch_byte = 0;
            if (offset < 64) begin
                if (offset < 16) rom_batch_byte = CRT_MAGIC[(15-offset)*8 +: 8];
                case (offset)
                    19: rom_batch_byte = 8'h40;
                    20: rom_batch_byte = 1;
                    23: rom_batch_byte = id;
                    24: rom_batch_byte = (id == 71) ^ (variant == 2);
                    25: rom_batch_byte = id == 65 || id == 86;
                    26: rom_batch_byte = variant == 1;
                    default: ;
                endcase
            end else begin
                packet = (offset-64)/(size+16);
                local_offset = (offset-64)%(size+16);
                case (local_offset)
                    0: rom_batch_byte = "C"; 1: rom_batch_byte = "H";
                    2: rom_batch_byte = "I"; 3: rom_batch_byte = "P";
                    6: rom_batch_byte = (size+16)>>8;
                    7: rom_batch_byte = 8'h10;
                    9: rom_batch_byte = variant == 6 ? 2 : 0;
                    11: rom_batch_byte = variant == 3 ? 1 : packet;
                    12: rom_batch_byte = variant == 5 ? 8'ha0 : 8'h80;
                    14: rom_batch_byte = variant == 4 ? 8'h10 : size>>8;
                    default: if (local_offset >= 16) rom_batch_byte = packet ^ offset;
                endcase
            end
        end
    endfunction

    task automatic rom_batch;
        integer id, size, banks, actual_banks, total, before_bytes, before_banks, before_words;
        for (integer profile=0;profile<8;profile++) begin
            case(profile)
                0:begin id=11;size=16384;banks=1;end
                1:begin id=54;size=8192;banks=3;end
                2:begin id=64;size=16384;banks=2;end
                3:begin id=65;size=8192;banks=1;end
                4:begin id=66;size=16384;banks=1;end
                5:begin id=71;size=16384;banks=2;end
                6:begin id=86;size=8192;banks=4;end
                default:begin id=87;size=16384;banks=4;end
            endcase
            for(integer variant=0;variant<8;variant++) begin
                actual_banks=variant==7?(banks==1?2:3):banks;
                if(variant==7&&id==54)actual_banks=2;
                total=64+actual_banks*(size+16);
                clear_error=1;@(negedge clk);clear_error=0;
                before_bytes=cart_write_count;before_banks=bank_write_count;before_words=cart_word_count;
                for(integer a=0;a<total;a++)
                    send_byte(MT_LOAD_C64_CRT,a,total,rom_batch_byte(a,id,size,variant));
                wait(!active);@(negedge clk);
                if(variant==0)begin
                    check(!error&&cart_loaded&&cart_id==id&&cart_subtype==0,
                        "ROM batch canonical image commits");
                    check(cart_expansion_policy==((id==54||id==71||id>=86)?7:0),
                        "ROM batch exact IO coexistence policy");
                    check(cart_write_count-before_bytes==banks*size&&
                        bank_write_count-before_banks==banks,"ROM batch complete payload and bank table");
                    check(cart_word_count-before_words==banks*size/8&&cart_partial_count==0,
                        "ROM batch payload written as whole 64-bit words");
                end else check(error&&!cart_loaded,
                    "ROM batch rejects subtype/lines/order/size/address/flash/count");
            end
            $display("ROM_LOADER PASS id=%0d positive=1 malformed=7",id);
        end
        check(dut.crt_layout_complete(MT_CRT_BLACKBOX_V8,4,1),"BB8 full four-bank shape");
        check(dut.crt_chip_layout_supported(MT_CRT_MEGABYTER,255,255,16'h8000,16'h2000,0,1)&&
            !dut.crt_chip_layout_supported(MT_CRT_MEGABYTER,256,0,16'h8000,16'h2000,0,1),
            "Megabyter 256 physical banks and no wrapped packet");
        check(dut.crt_layout_complete(MT_CRT_MEGABYTER,256,0)&&
            !dut.crt_layout_complete(MT_CRT_MEGABYTER,257,0),"Megabyter two MiB limit");
    endtask

    initial begin : test
        integer index;
        localparam integer CRT_SIZE = 80 + 8192;

        repeat (2) @(negedge clk);
        reset = 1'b0;

        send_byte(MT_LOAD_C64_PRG_RUN, 0, 1, 8'h00);
        check(error && run_request_count == 0,
              "explicit RUN rejected before a PRG is loaded");
        clear_error = 1'b1;
        @(negedge clk);
        clear_error = 1'b0;

        send_byte(MT_LOAD_C64_PRG, 0, 5, 8'h01);
        repeat (260) @(negedge clk);
        check(active && !load_ready,
              "PRG payload waits for KERNAL readiness");
        core_addr = 16'hffcf;
        send_byte(MT_LOAD_C64_PRG, 1, 5, 8'h08);
        check(prg_install_pause,
              "ordinary PRG pauses the core while installing payload");
        send_byte(MT_LOAD_C64_PRG, 2, 5, 8'ha9);
        send_byte(MT_LOAD_C64_PRG, 3, 5, 8'h00);
        send_byte(MT_LOAD_C64_PRG, 4, 5, 8'h60);
        wait (!active);
        @(negedge clk);
        check(prg_loaded, "PRG committed");
        check(ram[16'h0801] == 8'ha9 && ram[16'h0803] == 8'h60,
              "PRG payload placed at header address");
        check(ram[16'h002d] == 8'h04 && ram[16'h002e] == 8'h08 &&
              ram[16'h00ae] == 8'h04 && ram[16'h00af] == 8'h08,
              "BASIC end pointers initialized");
        check(prg_run_request,
              "ordinary PRG emits its automatic RUN pulse");
        @(negedge clk);
        check(run_request_count == 1,
              "ordinary PRG retains automatic RUN request");

        core_addr = 16'h0000;
        send_byte(MT_LOAD_C64_PRG_STAGE, 0, 5, 8'h01);
        repeat (260) @(negedge clk);
        check(active && !load_ready,
              "staged PRG payload waits for KERNAL readiness");
        core_addr = 16'hffcf;
        send_byte(MT_LOAD_C64_PRG_STAGE, 1, 5, 8'h09);
        check(prg_install_pause,
              "staged PRG pauses the core while installing payload");
        send_byte(MT_LOAD_C64_PRG_STAGE, 2, 5, 8'ha2);
        send_byte(MT_LOAD_C64_PRG_STAGE, 3, 5, 8'h2a);
        send_byte(MT_LOAD_C64_PRG_STAGE, 4, 5, 8'h60);
        wait (!active);
        @(negedge clk);
        check(prg_loaded, "staged PRG committed");
        check(ram[16'h0901] == 8'ha2 && ram[16'h0903] == 8'h60,
              "staged PRG payload placed at header address");
        check(ram[16'h002d] == 8'h04 && ram[16'h002e] == 8'h09 &&
              ram[16'h00ae] == 8'h04 && ram[16'h00af] == 8'h09,
              "staged PRG repairs BASIC end pointers");
        check(run_request_count == 1,
              "staged PRG does not request RUN");

        send_byte(MT_LOAD_C64_PRG_RUN, 0, 1, 8'h01);
        check(error && run_request_count == 1,
              "explicit RUN requires a zero token");
        clear_error = 1'b1;
        @(negedge clk);
        clear_error = 1'b0;
        send_byte(MT_LOAD_C64_PRG_RUN, 0, 1, 8'h00);
        check(!active && !prg_install_pause && !core_reset_hold,
              "explicit RUN token does not reset or pause the core");
        @(negedge clk);
        check(!error && run_request_count == 2,
              "explicit RUN accepted after staged PRG");

        for (index = 0; index < CRT_SIZE; index = index + 1)
            send_byte(MT_LOAD_C64_CRT, index, CRT_SIZE, crt_byte(index));
        wait (!active);
        @(negedge clk);
        check(cart_loaded && cart_id == MT_CRT_MAGIC_DESK,
              "supported CRT committed");
        check(cart_write_count == 8192 && bank_write_count == 1,
              "one CHIP payload normalized to external memory");
        check(cart_word_count == 1024 && cart_partial_count == 0,
              "CHIP payload written as whole 64-bit words");

        // BEGIN may have completed in the control domain even though its first
        // byte has not reached this loader. Repeated aborts for that epoch are
        // idempotent and quarantine the delayed first byte while idle.
        abort_stream(1'b0);
        abort_stream(1'b0);
        send_byte(MT_LOAD_C64_PRG_STAGE, 0, 5, 8'h01);
        check(prg_loaded && cart_loaded && !active && !error,
              "pre-first-byte repeated abort quarantines delayed first beat");

        // Abort while a replacement PRG is waiting for the freshly reset
        // KERNAL. The committed cartridge is unrelated state and must remain
        // selected. A late first beat from the old mailbox epoch is consumed
        // without recreating the transaction.
        core_addr = 16'h0000;
        load_epoch = 1'b1;
        send_byte(MT_LOAD_C64_PRG_STAGE, 0, 5, 8'h01);
        repeat (260) @(negedge clk);
        check(active && !load_ready && cart_loaded,
              "WAIT_BASIC abort setup retains committed CRT");
        abort_stream(1'b1);
        check(!active && load_ready && !core_reset_hold &&
              !prg_install_pause && cart_loaded &&
              cart_id == MT_CRT_MAGIC_DESK,
              "WAIT_BASIC abort recovers without clearing committed CRT");
        send_byte(MT_LOAD_C64_PRG_STAGE, 0, 5, 8'h01);
        check(!active && !error && cart_loaded,
              "queued first beat from aborted epoch is quarantined");

        // Enter the payload stream on a new epoch, write one byte, and abort.
        // A queued continuation from that epoch must likewise drain harmlessly.
        load_epoch = 1'b0;
        send_byte(MT_LOAD_C64_PRG_STAGE, 0, 5, 8'h01);
        repeat (260) @(negedge clk);
        core_addr = 16'hffcf;
        send_byte(MT_LOAD_C64_PRG_STAGE, 1, 5, 8'h0a);
        send_byte(MT_LOAD_C64_PRG_STAGE, 2, 5, 8'hea);
        check(active && prg_install_pause && cart_loaded,
              "partial PRG stream is active before abort");
        abort_stream(1'b0);
        clear_error = 1'b1;
        @(negedge clk);
        clear_error = 1'b0;
        send_byte(MT_LOAD_C64_PRG_STAGE, 3, 5, 8'h00);
        check(!active && load_ready && !error && !core_reset_hold &&
              !prg_install_pause && cart_loaded,
              "partial-stream abort drains stale beat without a new error");

        // A fresh epoch must be usable immediately after recovery and still
        // retain the staged-PRG versus explicit-RUN contract.
        core_addr = 16'h0000;
        load_epoch = 1'b1;
        send_byte(MT_LOAD_C64_PRG_STAGE, 0, 5, 8'h01);
        repeat (260) @(negedge clk);
        core_addr = 16'hffcf;
        send_byte(MT_LOAD_C64_PRG_STAGE, 1, 5, 8'h0b);
        send_byte(MT_LOAD_C64_PRG_STAGE, 2, 5, 8'ha9);
        send_byte(MT_LOAD_C64_PRG_STAGE, 3, 5, 8'h2a);
        send_byte(MT_LOAD_C64_PRG_STAGE, 4, 5, 8'h60);
        wait (!active);
        @(negedge clk);
        check(prg_loaded && cart_loaded && run_request_count == 2,
              "fresh staged PRG commits after abort without implicit RUN");

        // An idle abort is transport cleanup, not a content reset. Preserve
        // both committed content flags and ignore a late byte from its epoch.
        abort_stream(1'b1);
        send_byte(MT_LOAD_C64_PRG_STAGE, 1, 5, 8'h0b);
        check(prg_loaded && cart_loaded && !active && !error,
              "abort preserves committed PRG and CRT state");

        load_epoch = 1'b0;
        send_byte(MT_LOAD_C64_CRT_DETACH, 0, 1, 8'h00);
        check(!cart_loaded, "CRT detach is atomic");

        clear_error = 1'b1;
        @(negedge clk);
        clear_error = 1'b0;
        for (index = 0; index < 80; index = index + 1)
            send_byte(MT_LOAD_C64_CRT, index, 80,
                      index == 23 ? 8'd32 : crt_byte(index));
        check(error && !cart_loaded, "unsupported CRT rejected detached");

        for (integer variant = 0; variant < 5; variant++) begin
            clear_error = 1;
            @(negedge clk); clear_error = 0;
            for (index = 0; index < (variant == 4 ? 49264 : 65664); index++)
                send_byte(MT_LOAD_C64_CRT, index, variant == 4 ? 49264 : 65664,
                    fc3_byte(index, variant == 2 ? 2 : variant == 0 ? 0 : 1,
                             variant == 3));
            wait (!active); @(negedge clk);
            if (variant < 2) begin
                check(!error && cart_loaded && cart_id == MT_CRT_FINAL_CARTRIDGE_3 &&
                      cart_subtype == variant, "FC3 exact four-bank image commits subtype");
                check(cart_expansion_policy == (variant == 1 ? 3 : 0),
                      "FC3 subtype determines REU and UCI compatibility");
            end else check(error && !cart_loaded,
                "FC3 rejects unknown subtype, duplicate bank and incomplete image");
        end
        clear_error = 1; @(negedge clk); clear_error = 0;
        for (index = 0; index < CRT_SIZE; index++)
            send_byte(MT_LOAD_C64_CRT, index, CRT_SIZE,
                      index == 26 ? 8'd1 : crt_byte(index));
        wait (!active); @(negedge clk);
        check(error && !cart_loaded, "non-FC3 unknown subtype is rejected");

        // TMB accepts only canonical power-of-two 16 KiB banks within the
        // existing 2 MiB storage window. The upper boundary is also checked
        // directly so this unit bench need not stream 2 MiB repeatedly.
        check(dut.crt_chip_layout_supported(MT_CRT_TWO_MEGABYTER,
                  127,127,16'h8000,16'h4000,0,0) &&
              !dut.crt_chip_layout_supported(MT_CRT_TWO_MEGABYTER,
                  128,128,16'h8000,16'h4000,0,0),
              "TMB indexed bank limit is 128");
        check(dut.crt_layout_complete(MT_CRT_TWO_MEGABYTER,128,1) &&
              !dut.crt_layout_complete(MT_CRT_TWO_MEGABYTER,256,1),
              "TMB admits 2 MiB and rejects 4 MiB");
        for(integer variant=0;variant<8;variant++)begin
            integer banks,total;
            banks=variant==0?1:variant==1?16:variant==2?3:4;
            total=64+banks*16400;
            clear_error=1;@(negedge clk);clear_error=0;
            for(index=0;index<total;index++)
                send_byte(MT_LOAD_C64_CRT,index,total,tmb_byte(index,variant));
            wait(!active);@(negedge clk);
            if(variant<2)check(!error&&cart_loaded&&cart_id==MT_CRT_TWO_MEGABYTER&&
                cart_subtype==0&&cart_expansion_policy==7,"TMB canonical image and expansion policy");
            else check(error&&!cart_loaded,"TMB rejects malformed size/order/subtype/lines");
        end
        clear_error=1;@(negedge clk);clear_error=0;
        send_byte(MT_LOAD_C64_CRT,0,4198464,"C");
        check(error&&!cart_loaded,"TMB 4 MiB transfer is rejected at admission");

        rom_batch();

        $display("PASS: C64 PRG and CRT content loader");
        $finish;
    end
endmodule

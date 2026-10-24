// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_c64_sid_transport #(parameter bit SHARED_CORE_RUNNING=1);
    reg host_clk=0,core_clk=0;always #5 host_clk=~host_clk;always #7 core_clk=~core_clk;
    reg host_resetn=0,core_resetn=0;
    reg [5:0] host_index=0;reg [31:0] host_data=0;reg host_write=0,host_commit=0;
    reg [7:0] host_sequence=0;wire [31:0] host_status;
    reg paused=0;reg [4:0] phase=0;always @(posedge core_clk)phase<=phase+1'b1;
    wire ce_1m=phase==0&&!paused;
    wire active_valid;wire [255:0] slot_masks,chip_params;wire [31:0] write_inhibit,output_config;
    wire [95:0] owners;wire [127:0] io_slot_masks;
    reg [7:0] controls_value=8'ha5;
    wire [7:0] controls_snapshot;
    wire core_running_host;
    if(SHARED_CORE_RUNNING)begin : shared_controls
        multitini_word_mailbox #(.WIDTH(8)) controls_i(
            .src_clk(host_clk),.src_resetn(host_resetn),.value(controls_value),
            .dst_clk(core_clk),.dst_resetn(core_resetn),.snapshot(controls_snapshot),
            .destination_running(core_running_host));
    end else begin : local_controls
        assign controls_snapshot=controls_value;
        assign core_running_host=1'b0;
    end
    multitini_c64_sid_transport #(.SHARED_CORE_RUNNING(SHARED_CORE_RUNNING)) dut(.*);
    reg [31:0] words[0:56];integer checks=0,i,waits;
    task automatic step;@(posedge host_clk);#1;endtask
    task automatic check(input bit ok,input string reason);checks++;if(!ok)$fatal(1,"SID transport %s status=%x cursor=%d",reason,host_status,dut.cursor);endtask
    task automatic upload(input [7:0] first);
        for(integer n=0;n<57;n++)begin @(negedge host_clk);host_write=1;host_index=n;host_data=n==0?first:words[n];step();end
        @(negedge host_clk);host_write=0;
    endtask
    task automatic commit_table(input [7:0] seq);
        @(negedge host_clk);host_sequence=seq;host_commit=1;step();@(negedge host_clk);host_commit=0;
    endtask
    task automatic write_word(input [5:0] index,input [31:0] value);
        @(negedge host_clk);host_index=index;host_data=value;host_write=1;step();
        @(negedge host_clk);host_write=0;
    endtask
    task automatic wait_done(input [7:0] seq);
        waits=0;while((host_status[8]||!host_status[31]||host_status[7:0]!=seq)&&waits<20000)begin step();waits++;end
        check(waits<20000&&active_valid,"bounded complete-table ACK");
        if(SHARED_CORE_RUNNING)check(controls_snapshot==controls_value&&
            dut.core_running==core_running_host,"table and controls recover from the same running level");
    endtask
    task automatic core_restart(input [7:0] preserved);
        @(negedge core_clk);core_resetn=0;controls_value=controls_value+1'b1;repeat(20)step();
        check(host_status[7:0]==preserved,"accepted sequence survives core reset");
        check(!active_valid,"native table invalid while core reset");
        if(SHARED_CORE_RUNNING)check(!core_running_host&&controls_snapshot==0,
            "shared reset holds controls and SID upload together");
        @(negedge core_clk);core_resetn=1;
    endtask
    initial begin
        $readmemh("table.hex",words);repeat(5)step();host_resetn=1;core_resetn=1;repeat(50)step();
        commit_table(1);check(host_status[9]&&!host_status[8]&&host_status[14:10]==1&&host_status[21:16]==0,"missing table rejects with index");
        upload(1);commit_table(1);check(host_status[8]&&!host_status[9],"valid commit starts");
        upload(8'hff);commit_table(2);check(host_status[9]&&host_status[14:10]==6,"commit while busy rejects");
        wait_done(1);check(slot_masks[7:0]==1,"next shadow cannot tear in-flight table");
        commit_table(2);wait_done(2);check(slot_masks[7:0]==255&&owners[2:0]==0,"queued shadow applied atomically");
        upload(8'h80);commit_table(3);
        while(dut.cursor<17)step();core_restart(2);wait_done(3);check(slot_masks[7:0]==128&&owners[2:0]==7,"mid-upload reset resends complete committed bank");
        core_restart(3);wait_done(3);check(slot_masks[7:0]==128,"post-commit reset restores without host upload");
        upload(0);commit_table(4);check(host_status[9]&&!host_status[8]&&host_status[14:10]==2&&host_status[21:16]==0,"zero slot rejected");
        write_word(0,1);write_word(7,32'h103);commit_table(4);
        check(host_status[14:10]==7&&host_status[21:16]==7,"write-inhibit must name one owner");
        write_word(7,words[7]);write_word(56,32'hc0000);commit_table(4);
        check(host_status[14:10]==5&&host_status[21:16]==56,"reserved headroom rejects");
        write_word(56,words[56]);write_word(60,1);commit_table(4);
        check(host_status[14:10]==5&&host_status[21:16]==60,"nonzero reserved entry rejects");
        write_word(60,0);commit_table(4);wait_done(4);
        check(host_status[21:9]==0,"accepted commit clears rejection metadata");
        upload(2);paused=1;commit_table(255);wait_done(255);check(slot_masks[7:0]==2,"paused swap without native CE");
        upload(4);commit_table(0);wait_done(0);check(slot_masks[7:0]==4,"sequence wrap");
        @(negedge host_clk);host_resetn=0;repeat(30)step();host_resetn=1;repeat(50)step();
        check(!host_status[31]&&!host_status[8],"host reset cannot adopt stale ACK");
        upload(8);commit_table(1);wait_done(1);check(slot_masks[7:0]==8,"one-sided host reset recovers");
        // Exercise the complete error-selection range through real host writes.
        // The lowest bad index is reported without starting or disturbing upload.
        upload(1);
        for(integer n=0;n<64;n++)begin
            write_word(n,32'h80000001);commit_table(2);
            check(host_status[9]&&!host_status[8]&&host_status[14:10]==5&&
                  host_status[21:16]==n,"every invalid entry reports its own index");
            write_word(n,n<57?words[n]:0);
        end
        write_word(55,32'h80000001);write_word(11,32'h80000001);commit_table(2);
        check(host_status[21:16]==11,"lowest error wins across tree halves");
        write_word(11,words[11]);commit_table(2);
        check(host_status[21:16]==55,"repair reveals remaining higher error");
        write_word(55,words[55]);commit_table(2);wait_done(2);
        // COMMIT clears completeness. An invalid reserved entry must not hide
        // any earlier missing required entry as the next table is filled.
        write_word(63,1);
        for(integer n=0;n<57;n++)begin
            commit_table(3);
            check(host_status[14:10]==1&&host_status[21:16]==n&&!host_status[8],
                  "first missing field precedes later invalid data");
            write_word(n,words[n]);
        end
        commit_table(3);
        check(host_status[14:10]==5&&host_status[21:16]==63&&!host_status[8],
              "reserved invalid remains after all required fields supplied");
        write_word(63,0);commit_table(3);wait_done(3);
        $display("PASS SID TRANSPORT checks=%0d",checks);$finish;
    end
    initial begin #5000000;$fatal(1,"SID transport timeout");end
endmodule

// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Exact request/command CDC and MMIO-snapshot blocks are extracted from the
// production top by test_apple_iigs_media_bridge.py. The small source model
// implements the IIgs adapter contract: every operation remains live until
// its completion, and a failed operation retries with VALID held high.
module tb_apple_iigs_media_bridge;
    parameter integer ACTIVE_MACHINE=5;
    import multitini_regs_pkg::*;
    logic control_clk=0,c64_clk=0;
    always #5 control_clk=~control_clk;
    always #8.73 c64_clk=~c64_clk;
    logic control_aresetn=0,c64_aresetn=0,c64_core_reset_request=0;
    logic [31:0] shell_control=0;
    logic c64_media_request_valid=0,c64_media_request_write=0;
    logic [31:0] c64_media_request_id=0,c64_media_request_lba=0,c64_media_request_generation=0;
    logic [15:0] c64_media_request_block_count=0;
    logic media_request_valid_control,media_request_write_control;
    logic [31:0] media_request_id_control,media_request_lba_control,media_request_generation_control;
    logic [15:0] media_request_block_count_control;
    logic media_mount=0,media_eject=0,media_complete=0;
    logic [7:0] media_slot=0,media_kind=8'h41;
    logic media_read_only=0;
    logic [31:0] media_image_size=65536,media_generation=99;
    logic [31:0] media_complete_request_id=0,media_complete_status=0,media_complete_generation=0;
    logic [15:0] media_complete_block_count=0;
    logic c64_media_mount,c64_media_eject,c64_media_complete;
    logic [7:0] c64_media_slot,c64_media_kind;
    logic c64_media_read_only;
    logic [31:0] c64_media_image_size,c64_media_generation;
    logic [31:0] c64_media_complete_request_id,c64_media_complete_status,c64_media_complete_generation;
    logic [15:0] c64_media_complete_block_count;
    logic media_submit_ready;
    logic [1:0] media_command_operation,c64_media_command_operation;
    logic [194:0] media_command_mailbox_source,media_command_mailbox_destination;
    logic media_command_submit,c64_media_command_valid;
    logic [112:0] media_request_mailbox_source,media_request_mailbox_destination;
    logic media_request_mailbox_src_valid,media_request_mailbox_src_ready;
    logic media_request_mailbox_dst_valid,media_request_mailbox_dst_ready;
    logic c64_media_request_sent;
    logic [31:0] c64_media_request_sent_id,c64_media_request_sent_generation;
`include "apple_iigs_media_bridge_production.svh"

    integer snapshots=0,commands=0,checks=0;
    logic [1:0] last_operation=0;
    logic [7:0] last_slot=0;
    logic hold_completion=0,retry_generation=0,completion_waiting=0;
    logic [31:0] held_id=0,held_generation=0,held_status=0;
    logic [4095:0] retained_buffer;
    logic [4095:0] original_buffer;
    integer core_retries=0,core_successes=0;
    always @(posedge control_clk)
        if(control_aresetn && media_request_mailbox_dst_valid && media_request_mailbox_dst_ready)
            snapshots<=snapshots+1;
    always @(posedge c64_clk) begin
        if(!c64_aresetn)begin
            completion_waiting<=0;core_retries<=0;core_successes<=0;
        end else begin
            if(c64_media_command_valid)begin
                commands<=commands+1;
                last_operation<=c64_media_command_operation;last_slot<=c64_media_slot;
                if(c64_media_complete)begin
                    if(completion_waiting)$fatal(1,"second completion overwrote an in-flight completion");
                    held_id<=c64_media_complete_request_id;
                    held_generation<=c64_media_complete_generation;
                    held_status<=c64_media_complete_status;
                    completion_waiting<=1;
                end
            end
            if(completion_waiting && !hold_completion)begin
                if(c64_media_request_valid && held_id==c64_media_request_id && held_generation==c64_media_request_generation)begin
                    if(held_status==MT_RESPONSE_OK)begin
                        c64_media_request_valid<=0;core_successes<=core_successes+1;
                    end else begin
                        // Retained native floppy buffer and request tuple:
                        // only its identity advances; VALID never goes low.
                        if(retry_generation)c64_media_request_generation<=c64_media_request_generation+1'b1;
                        else c64_media_request_id<=c64_media_request_id+1'b1;
                        core_retries<=core_retries+1;
                    end
                end
                completion_waiting<=0;
            end
        end
    end
    task automatic mark(input string message);
        begin checks++;$display("PASS: media bridge %s",message);end
    endtask
    task automatic settle;
        begin repeat(40)@(negedge control_clk);end
    endtask
    task automatic offer(input bit writing,input byte slot,input logic [31:0] id,input logic [31:0] generation);
        begin
            if(c64_media_request_valid)$fatal(1,"fixture offered over an outstanding request");
            @(negedge c64_clk);
            c64_media_request_write=writing;c64_media_request_id=id;c64_media_request_generation=generation;
            c64_media_request_lba=32'h10203040+slot;c64_media_request_block_count={slot,8'd2};
            for(integer i=0;i<512;i++)retained_buffer[i*8+:8]=8'(i^id^generation);
            original_buffer=retained_buffer;c64_media_request_valid=1;
        end
    endtask
    task automatic wait_snapshot(input logic [31:0] id,input logic [31:0] generation,input string scenario);
        integer polls;
        begin
            polls=0;
            while(!(media_request_valid_control && media_request_id_control==id && media_request_generation_control==generation) && polls<300)begin
                @(negedge control_clk);polls++;
            end
            if(polls==300)$fatal(1,"%s: request snapshot did not arrive id%08x gen%08x valid%b oldid%08x",scenario,id,generation,media_request_valid_control,media_request_id_control);
            if(media_request_write_control!==c64_media_request_write || media_request_lba_control!==c64_media_request_lba ||
               media_request_block_count_control!==c64_media_request_block_count)
                $fatal(1,"%s: incoherent request snapshot",scenario);
            if(retained_buffer!==original_buffer)$fatal(1,"retained retry buffer changed");
        end
    endtask
    task automatic send_command(input logic [1:0] operation,input byte slot,input logic [31:0] status);
        integer count_before,polls;
        begin
            polls=0;
            while(!media_submit_ready && polls<300)begin @(negedge control_clk);polls++;end
            if(!media_submit_ready)$fatal(1,"media command mailbox never became ready");
            @(negedge control_clk);count_before=commands;
            media_slot=slot;media_generation=32'h5a5aa5a5;
            media_complete_request_id=media_request_id_control;
            media_complete_generation=media_request_generation_control;
            media_complete_status=status;
            media_complete_block_count=status==MT_RESPONSE_OK ? media_request_block_count_control:0;
            media_mount=operation==1;media_eject=operation==2;media_complete=operation==3;
            @(negedge control_clk);media_mount=0;media_eject=0;media_complete=0;
            polls=0;
            while(commands==count_before && polls<300)begin @(negedge control_clk);polls++;end
            if(commands!=count_before+1 || last_operation!==operation || last_slot!==slot)
                $fatal(1,"command tuple did not cross exactly once");
        end
    endtask
    task automatic assert_snapshot_retained(input string scenario);
        begin
            settle();
            if(!media_request_valid_control)$fatal(1,"%s cleared a retained read/write request",scenario);
            wait_snapshot(c64_media_request_id,c64_media_request_generation,scenario);
        end
    endtask
    task automatic finish;
        integer polls;
        begin
            send_command(3,0,MT_RESPONSE_OK);polls=0;
            while(c64_media_request_valid && polls<300)begin @(negedge c64_clk);polls++;end
            if(c64_media_request_valid)$fatal(1,"successful completion failed to release the source request");
            settle();
            if(media_request_valid_control)$fatal(1,"completed request was replayed");
        end
    endtask
    task automatic read_lifecycle;
        integer snapshots_before;
        begin
            for(integer selected=0;selected<2;selected++)begin
                snapshots_before=snapshots;
                offer(0,selected ? 2:0,32'h100+selected,32'h1000);
                wait_snapshot(32'h100+selected,32'h1000,"initial read");
                send_command(2,2,0);assert_snapshot_retained(selected ? "target EJECT":"unrelated EJECT");
                send_command(1,3,0);assert_snapshot_retained("unrelated MOUNT");
                hold_completion=1;
                send_command(3,0,MT_RESPONSE_OK);settle();
                if(media_request_valid_control || !c64_media_request_valid || !completion_waiting)
                    $fatal(1,"completion in flight lost source retention or replayed its snapshot");
                hold_completion=0;settle();
                if(media_request_valid_control || c64_media_request_valid || snapshots!=snapshots_before+1)
                    $fatal(1,"read completion did not release exactly once");
            end
            mark("target and unrelated read snapshots survive EJECT/MOUNT; in-flight completion clears once without replay");
        end
    endtask
    task automatic write_lifecycle;
        integer snapshots_before;
        begin
            snapshots_before=snapshots;offer(1,0,32'h200,32'h2000);wait_snapshot(32'h200,32'h2000,"initial write");
            send_command(2,2,0);assert_snapshot_retained("unrelated EJECT");
            send_command(1,3,0);assert_snapshot_retained("unrelated MOUNT write");
            finish();if(snapshots!=snapshots_before+1)$fatal(1,"write request replayed during unrelated metadata command");
            mark("unrelated MOUNT/EJECT preserve a reverse write and its exact completion");
        end
    endtask
    task automatic crossing_metadata;
        integer snapshots_before;
        logic [31:0] id;
        begin
            // Sweep metadata submission across the asynchronous publication
            // window, including commands sent before the ARM snapshot exists.
            for(integer operation=1;operation<=2;operation++) for(integer phase_offset=0;phase_offset<6;phase_offset++)begin
                id=32'h800+operation*16+phase_offset;snapshots_before=snapshots;
                offer(0,0,id,32'h8000);
                fork
                    begin
                        repeat(phase_offset)@(negedge control_clk);
                        send_command(operation[1:0],2,0);
                    end
                    wait_snapshot(id,32'h8000,"read crossing during metadata command");
                join
                assert_snapshot_retained("MOUNT/EJECT during read publication");
                finish();
                if(snapshots!=snapshots_before+1)$fatal(1,"crossing metadata command lost or duplicated a read");
            end
            mark("MOUNT/EJECT at twelve asynchronous request-publication phases retain and complete each read once");
        end
    endtask
    task automatic fresh_retry;
        logic [31:0] id,generation;
        integer snapshots_before;
        begin
            for(integer writing=0;writing<2;writing++) for(integer by_generation=0;by_generation<2;by_generation++)begin
                snapshots_before=snapshots;retry_generation=by_generation;
                id=32'hfffffffe;generation=32'h3000+writing*16+by_generation;
                offer(writing,2,id,generation);wait_snapshot(id,generation,"first retained operation");
                for(integer retry=0;retry<4;retry++)begin
                    send_command(3,0,MT_RESPONSE_IO_ERROR);
                    if(by_generation)generation=generation+1'b1;else id=id+1'b1;
                    wait_snapshot(id,generation,"fresh retained retry never crossed");
                    settle();
                    if(!c64_media_request_valid || snapshots!=snapshots_before+retry+2)
                        $fatal(1,"fresh retained identity did not cross exactly once with VALID held high");
                    send_command(2,2,0);assert_snapshot_retained("EJECT during retained retry");
                end
                finish();
            end
            retry_generation=0;
            mark("read/write failures retry four times with VALID held high, fresh ID or generation, ID wrap, retained tuple/buffer and no duplicate");
        end
    endtask
    task automatic legacy_behavior;
        integer snapshots_before;
        begin
            snapshots_before=snapshots;offer(0,0,1,1);wait_snapshot(1,1,"legacy initial read");
            send_command(2,2,0);settle();if(media_request_valid_control)$fatal(1,"legacy read EJECT cancellation changed");
            @(negedge c64_clk);c64_media_request_id=2;settle();
            if(media_request_valid_control || snapshots!=snapshots_before+1 || media_request_mailbox_src_valid)
                $fatal(1,"legacy held-VALID identity policy changed");
            @(negedge c64_clk);c64_media_request_valid=0;settle();
            offer(1,0,3,1);wait_snapshot(3,1,"legacy write");
            send_command(2,2,0);settle();if(!media_request_valid_control)$fatal(1,"legacy dirty-write EJECT retention changed");
            send_command(1,2,0);settle();if(media_request_valid_control)$fatal(1,"legacy MOUNT cancellation changed");
            @(negedge c64_clk);c64_media_request_valid=0;settle();
            mark($sformatf("machine%0d retains legacy EJECT/MOUNT cancellation and held-VALID retry policy",ACTIVE_MACHINE));
        end
    endtask
    task automatic reset_identity;
        begin
            shell_control=MT_CONTROL_CORE_RESET_REQUEST;c64_core_reset_request=1;
            settle();
            if(media_request_valid_control || c64_media_request_sent || c64_media_request_sent_id || c64_media_request_sent_generation)
                $fatal(1,"soft-reset sent identity or MMIO snapshot did not clear");
            shell_control=0;c64_core_reset_request=0;settle();
            offer(0,2,32'h100,32'h1000);wait_snapshot(32'h100,32'h1000,"same identity after reset");finish();
            mark("soft reset clears sent identity and MMIO snapshot; the same future identity can cross again");
        end
    endtask
    task automatic retained_reset_publication;
        integer snapshots_before,polls;
        logic [31:0] id,generation;
        begin
            // The real IIgs boot path mounts floppy metadata while guest
            // reset is still asserted. The adapter retains this LBA0 read;
            // guest reset does not reset the WOZ controller or its transfer.
            // Sweep release against an active four-phase mailbox handshake.
            for(integer retained_before_reset=0;retained_before_reset<2;retained_before_reset++)
            for(integer release_phase=0;release_phase<32;release_phase++)begin
                id=32'h4000+retained_before_reset*256+release_phase*2;
                generation=32'h1500+retained_before_reset*256+release_phase;
                if(retained_before_reset)begin
                    offer(0,2,id,generation);c64_media_request_lba=0;
                    repeat(release_phase%16)@(negedge control_clk);
                end
                @(negedge control_clk);shell_control=MT_CONTROL_CORE_RESET_REQUEST;
                repeat(3)@(negedge c64_clk);c64_core_reset_request=1;
                settle();snapshots_before=snapshots;
                if(!retained_before_reset)begin offer(0,2,id,generation);c64_media_request_lba=0;end
                repeat(48+release_phase)@(negedge c64_clk);
                @(negedge control_clk);shell_control=0;
                repeat(3)@(negedge c64_clk);c64_core_reset_request=0;
                wait_snapshot(id,generation,"retained remount LBA0 after guest reset");
                send_command(3,0,MT_RESPONSE_OK);polls=0;
                while(c64_media_request_valid && polls<300)begin @(negedge c64_clk);polls++;end
                if(c64_media_request_valid)$fatal(1,"retained remount read did not complete");
                // A second copy of LBA0 must not reach ARM after completion:
                // read-stream bytes are untagged, so its WOZ signature could
                // otherwise overwrite the next metadata block (LBA1).
                offer(0,2,id+1,generation);c64_media_request_lba=1;polls=0;
                while(!(media_request_valid_control && media_request_id_control==id+1) && polls<300)begin
                    @(negedge control_clk);polls++;
                    if(media_request_valid_control && media_request_id_control==id)
                        $fatal(1,"guest reset replayed stale LBA0 header over subsequent LBA1 metadata release_phase=%0d",release_phase);
                end
                wait_snapshot(id+1,generation,"next LBA1 metadata after retained remount read");finish();
                if(snapshots!=snapshots_before+2)
                    $fatal(1,"guest reset published a retained read more than once release_phase=%0d snapshots=%0d",release_phase,snapshots-snapshots_before);
            end
            mark("guest reset retains pre-reset and remount metadata reads and publishes exactly once after release at 64 mailbox phases; no stale LBA0 can overwrite LBA1");
        end
    endtask
    initial begin
        repeat(5)@(negedge control_clk);control_aresetn=1;
        repeat(5)@(negedge c64_clk);c64_aresetn=1;settle();
        if(ACTIVE_MACHINE==5)begin
            if($test$plusargs("ONLY_RESET_RETAINED"))retained_reset_publication();
            else if($test$plusargs("ONLY_RETRY"))fresh_retry();
            else if($test$plusargs("ONLY_MOUNT_WRITE"))write_lifecycle();
            else begin read_lifecycle();write_lifecycle();crossing_metadata();fresh_retry();reset_identity();retained_reset_publication();end
        end else legacy_behavior();
        $display("CHECKS completed=%0d machine=%0d snapshots=%0d commands=%0d",checks,ACTIVE_MACHINE,snapshots,commands);$finish;
    end
    initial begin #1000000;$fatal(1,"media bridge regression timeout");end
endmodule

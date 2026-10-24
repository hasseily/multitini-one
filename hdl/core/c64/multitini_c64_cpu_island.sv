// SPDX-License-Identifier: GPL-3.0-only
// M0 draft. Related-clock crossings are timed, not asynchronous CDC paths.
// The unchanged production cpu_6510/T65 remains the only instruction engine.
`timescale 1ns/1ps
module multitini_c64_cpu_island #(
    parameter integer FIO_DEPTH=4, VIC_READ_GAP=2,
    parameter FAST_VIC_IO=1, FAST_SID_IO=1, FAST_COLOUR_RAM=1,
    parameter FORWARD_PORT_WRITES=0, FF00_SNOOP=1,
    parameter integer PTR_W=$clog2(FIO_DEPTH)+1
) (
    input wire clk_cpu, c64_aresetn, ext_reset,
    input wire [4:0] ext_phase,
    input wire ext_frame_run, ext_irq_n, ext_nmi_n, ext_rdy, ext_dma_hold,
    input wire pause_request,diag_armed,
    input wire [7:0] ext_last_vic_di, external_port_input,
    input wire [7:0] b0_data,
    input wire b0_irq_n,b0_nmi_n,b0_rdy,b0_toggle,b0_exact,b0_seq,
    input wire b1_toggle,b1_dma_hold,b1_turbo,b1_badline,b1_pause,b1_c64u_top,
    input wire [3:0] b1_index,
    input wire port_tick_toggle,irq_safe_toggle,
    output reg [15:0] isl_x_req_addr=0,
    output reg [7:0] isl_x_req_wdata=0,isl_x_req_port=0,
    output reg isl_x_req_we=0,isl_x_req_exact=1,isl_x_req_ioonly=0,
    output reg isl_x_req_seq=0,isl_x_req_valid=0,
    output wire cpu_mem_ce,cpu_mem_ram_we,cpu_mem_colour_we,
    output wire [15:0] cpu_mem_addr,
    output wire [7:0] cpu_mem_wdata,
    input wire [7:0] ram_low_q,ram_high_q,rom_q,char_q,
    input wire [3:0] colour_q,
    output wire [9:0] fio_rd_addr,
    output wire fio_rd_sid,
    input wire [7:0] fio_rd_data,
    input wire fio_vic_busy,
    input wire [PTR_W-1:0] fio_done,
    output wire [FIO_DEPTH*22-1:0] fio_entries,
    output reg [PTR_W-1:0] fio_wp=0,
    output wire nmi_ack,
    output reg diag_nmi_used=0,
    output wire [15:0] cpu_address,
    output wire [7:0] cpu_wdata,cpu_port,
    output wire cpu_write,cpu_enable,
    output reg turbo_active=0,
    output reg [3:0] active_index=0,
    output reg active_badline=1,active_c64u_top=0,
    output wire [5:0] fast_slot
);
    localparam EXACT=0,RUN=1,FWD=2,COMPLETE=3,ACKWAIT=4,DMAWAIT=5;
    localparam WR=0,CLR_M2M=1,CLR_M2D=2,LATCH=3;
    localparam SLOT_W=(FIO_DEPTH<=1)?1:$clog2(FIO_DEPTH);
    reg [2:0] state=EXACT;
    reg reset_q=1;
    reg [4:0] phase_q=0;
    reg frame_run_q=0,run_q=0,ba_q=1,irq_q=1,nmi_q=1,dma_q=0,busy_q=0;
    reg [7:0] last_vic_q=0,port_input_q=8'h17;
    reg tick_seen=0,irq_marker_q=0,irq_marker_seen=0;
    reg b0_seen=0,b1_seen=0,en_q=0,en_qq=0;
    reg forward_is_ack=0,ff00_stored=0;
    reg [7:0] io2_q=0;
    reg [5:0] di_select=6'b100000;
    wire [7:0] cpu_di=({8{di_select[0]}}&ram_low_q) |
                     ({8{di_select[1]}}&ram_high_q) |
                     ({8{di_select[2]}}&rom_q) |
                     ({8{di_select[3]}}&char_q) |
                     ({8{di_select[4]}}&io2_q) |
                     ({8{di_select[5]}}&b0_data);
    wire map_ok,local_cycle,local_write,port_write,colour_cycle;
    wire fio_cycle,forward_cycle,forward_ioonly,ff00_local,forward_ack,ram_store,colour_store;
    wire [2:0] read_source;
    wire halt_q=active_badline && !ba_q;
    wire grant_q,credit_q;
    wire all_done_q,fetch_done_q;
    reg wr_pend_q=0,clr_m2m_pend_q=0,clr_m2d_pend_q=0,latch_pend_q=0;
    reg full_q=0,ack_wait_q=0;
    reg all_done_reg=1,fetch_done_reg=1;
    reg [PTR_W-1:0] ack_end=0;
    assign all_done_q=all_done_reg;
    assign fetch_done_q=fetch_done_reg;
    reg [21:0] entries[0:FIO_DEPTH-1];
    reg [21:0] entries_next[0:FIO_DEPTH-1];
    genvar g;
    generate for(g=0;g<FIO_DEPTH;g=g+1) begin: fio_flat
        assign fio_entries[g*22+:22]=entries[g];
    end endgenerate
    wire vic_collision=!cpu_address[10] &&
        (cpu_address[5:0]==6'h1e || cpu_address[5:0]==6'h1f);
    wire sid_live=cpu_address[10] && cpu_address[4:0]>=5'h19 && cpu_address[4:0]<=5'h1c;
    wire may_push=vic_collision || sid_live;
    wire dependency_ok=(!cpu_address[10] && cpu_address[5:0]==6'h1e) ? !clr_m2m_pend_q :
                       (!cpu_address[10] && cpu_address[5:0]==6'h1f) ? !clr_m2d_pend_q :
                       (cpu_address[10] && !sid_live) ? !latch_pend_q : 1'b1;
    wire read_gap_ok=!en_q && ((VIC_READ_GAP==2)||!en_qq);
    wire base=turbo_active && state==RUN && grant_q && run_q &&
              !halt_q && map_ok && !ack_wait_q && !reset_q;
    wire local_ok=base && local_cycle &&
        (cpu_write ? fetch_done_q : (!colour_cycle || read_gap_ok));
    wire fio_ok=base && fio_cycle && !busy_q &&
        (cpu_write ? !full_q : (read_gap_ok && !wr_pend_q && dependency_ok && (!may_push||!full_q)));
    wire fresh_forward_reply=state==FWD && b1_toggle!=b1_seen &&
        b0_seq==isl_x_req_seq && !b0_exact;
    wire completion_ready=((fresh_forward_reply && !b1_dma_hold && !forward_is_ack) || state==COMPLETE ||
        (state==ACKWAIT && irq_marker_q!=irq_marker_seen) ||
        (state==DMAWAIT && !dma_q && (!forward_is_ack || irq_marker_q!=irq_marker_seen)));
    wire completion_enable=turbo_active && completion_ready &&
        (credit_q||grant_q) && !halt_q && !ack_wait_q && run_q && !reset_q;
    wire exact_enable=!turbo_active && state==EXACT && b0_toggle!=b0_seen && !reset_q;
    assign cpu_enable=exact_enable || local_ok || fio_ok || completion_enable;
    wire nmi_safe,unused_rearm,t65_nmi_n,nmi_used;
    // Forced exact takes its normal boundary. The NMI helper carries any
    // unfinished delivery across the first exact enables without extra turbo.
    wire adopt=cpu_enable && all_done_q;
    wire policy_changed=adopt && (turbo_active!=b1_turbo || active_index!=b1_index ||
        active_badline!=b1_badline || active_c64u_top!=b1_c64u_top);
    wire forward_wait=turbo_active && ((state==RUN && forward_cycle) ||
                                      state==FWD || state==COMPLETE || state==ACKWAIT || state==DMAWAIT);
    wire ff00_write=base && ff00_local && fetch_done_q && !ff00_stored;
    wire issue_forward=turbo_active && state==RUN && forward_cycle && map_ok &&
        all_done_q && !ack_wait_q && run_q && !halt_q && (!ff00_local || ff00_stored || ff00_write);

    // At a CPU edge, native phase equal to its prior sampled value means
    // the second fast edge of that native phase. A changed phase means first.
    // Register the NEXT retiring slot's grant before its BRAM falling edge.
    assign fast_slot={ext_phase,(phase_q==ext_phase)};
    multitini_c64_island_governor governor_i (
        .clk(clk_cpu),.reset(reset_q),.turbo(turbo_active),.policy_changed(policy_changed),
        .rate_index(active_index),.c64u_top(active_c64u_top),
        .slot_next(fast_slot+6'd1),.frame_run_next(ext_frame_run),
        .forward_wait(forward_wait),.completion_enable(completion_enable),
        .grant_q(grant_q),.credit_q(credit_q)
    );
    multitini_c64_island_map #(
        .FAST_VIC_IO(FAST_VIC_IO),.FAST_SID_IO(FAST_SID_IO),.FAST_COLOUR_RAM(FAST_COLOUR_RAM),
        .FORWARD_PORT_WRITES(FORWARD_PORT_WRITES),.FF00_SNOOP(FF00_SNOOP)
    ) map_i (
        .clk(clk_cpu),.reset(reset_q),.port_write_retired(cpu_enable && cpu_write && cpu_address[15:1]==0),
        .port_pins(cpu_port),.addr(cpu_address),.write(cpu_write),.map_ok(map_ok),.read_source(read_source),
        .local_cycle(local_cycle),.local_write(local_write),.port_write(port_write),.colour_cycle(colour_cycle),
        .fio_cycle(fio_cycle),.forward_cycle(forward_cycle),.forward_ioonly(forward_ioonly),
        .ff00_local(ff00_local),.forward_ack(forward_ack),
        .ram_store(ram_store),.colour_store(colour_store)
    );
    multitini_c64_nmi_delivery nmi_i (
        .clk(clk_cpu),.reset(reset_q),.turbo(turbo_active),.t65_enable(cpu_enable),
        .live_nmi_n(nmi_q),.exact_nmi_n(b0_nmi_n),.t65_nmi_n(t65_nmi_n),
        .exact_switch_safe(nmi_safe),.pending_rearm(unused_rearm),.carry_active(),.used_hold(nmi_used)
    );
    always @(posedge clk_cpu) begin
        if(reset_q || !diag_armed) diag_nmi_used<=0;
        else if(nmi_used) diag_nmi_used<=1;
    end
    cpu_6510 cpu_i (
        .clk(clk_cpu),.enable(cpu_enable),.bus_start(1'b0),.reset(reset_q),
        .nmi_n(t65_nmi_n),.nmi_ack(nmi_ack),.irq_n(turbo_active?irq_q:b0_irq_n),
        .rdy(turbo_active?1'b1:b0_rdy),.port_tick(port_tick_toggle!=tick_seen && !reset_q),
        .di(cpu_di),.\do (cpu_wdata),.addr(cpu_address),.we(cpu_write),
        .diIO(port_input_q),.doIO(cpu_port)
    );
    assign cpu_mem_ce=turbo_active && state==RUN && grant_q && run_q && !halt_q && !ack_wait_q && !reset_q;
    // ff00_stored can be set only while this very CPU bus cycle is held for
    // its native snoop. It clears on that cycle's completion, before the next
    // address can store, so it replaces a second full-address FF00 mux here.
    assign cpu_mem_ram_we=cpu_mem_ce && map_ok && fetch_done_q && ram_store && !ff00_stored;
    assign cpu_mem_colour_we=cpu_mem_ce && map_ok && fetch_done_q && colour_store;
    assign cpu_mem_addr=cpu_address;
    assign cpu_mem_wdata=port_write?last_vic_q:cpu_wdata;
    assign fio_rd_addr=cpu_address[9:0];
    assign fio_rd_sid=cpu_address[10];
    always @(negedge clk_cpu) begin
        if(!turbo_active || state!=RUN) di_select<=6'b100000;
        else di_select<=6'b000001<<read_source;
    end

    wire push=fio_ok && (cpu_write || sid_live || (vic_collision && io2_q!=0));
    wire [1:0] push_kind=cpu_write?WR:sid_live?LATCH:cpu_address[0]?CLR_M2D:CLR_M2M;
    wire fetch_affecting=!cpu_address[10] && cpu_write &&
        (cpu_address[5:0]==6'h11 || cpu_address[5:0]==6'h15 || cpu_address[5:0]==6'h17 ||
         cpu_address[5:0]==6'h18 || (cpu_address[5:4]==0 && cpu_address[0]));
    wire ack_push=push && !cpu_address[10] && cpu_write &&
        (cpu_address[5:0]==6'h19 || cpu_address[5:0]==6'h1a);
    wire [21:0] push_entry={cpu_address[10],push_kind,fetch_affecting,cpu_address[9:0],cpu_write?cpu_wdata:io2_q};
    wire [PTR_W-1:0] wp_next=fio_wp+push;
    wire [PTR_W-1:0] pending_count=wp_next-fio_done;
    reg next_wr,next_m2m,next_m2d,next_latch,next_fetch;
    integer s,j;
    reg [PTR_W-1:0] walk;
    always @* begin
        for(s=0;s<FIO_DEPTH;s=s+1) entries_next[s]=entries[s];
        if(push) entries_next[(FIO_DEPTH==1)?0:fio_wp[SLOT_W-1:0]]=push_entry;
        next_wr=0;next_m2m=0;next_m2d=0;next_latch=0;next_fetch=0;
        for(j=0;j<FIO_DEPTH;j=j+1) begin
            walk=fio_done+j;
            if(j<pending_count) begin
                s=(FIO_DEPTH==1)?0:walk[SLOT_W-1:0];
                next_wr=next_wr || entries_next[s][20:19]==WR;
                next_m2m=next_m2m || entries_next[s][20:19]==CLR_M2M;
                next_m2d=next_m2d || entries_next[s][20:19]==CLR_M2D;
                next_latch=next_latch || entries_next[s][20:19]==LATCH;
                next_fetch=next_fetch || entries_next[s][18];
            end
        end
    end
    integer q;
    always @(posedge clk_cpu) begin
        reset_q<=ext_reset || !c64_aresetn;
        phase_q<=ext_phase;frame_run_q<=ext_frame_run;
        run_q<=!(pause_request||b1_pause);
        ba_q<=ext_rdy;irq_q<=ext_irq_n;nmi_q<=ext_nmi_n;dma_q<=ext_dma_hold;
        last_vic_q<=ext_last_vic_di;port_input_q<=external_port_input;busy_q<=fio_vic_busy;
        tick_seen<=port_tick_toggle;irq_marker_q<=irq_safe_toggle;
        io2_q<=colour_cycle?{last_vic_q[7:4],colour_q}:fio_rd_data;
        en_q<=cpu_enable;en_qq<=en_q;
        if(reset_q) begin
            turbo_active<=0;state<=EXACT;active_index<=0;active_badline<=1;active_c64u_top<=0;
            b0_seen<=b0_toggle;b1_seen<=b1_toggle;irq_marker_seen<=irq_marker_q;
            en_q<=0;en_qq<=0;forward_is_ack<=0;ff00_stored<=0;
            isl_x_req_addr<=cpu_address;isl_x_req_wdata<=cpu_wdata;isl_x_req_we<=cpu_write;
            isl_x_req_port<=cpu_port;isl_x_req_exact<=1;isl_x_req_ioonly<=0;
            isl_x_req_seq<=0;isl_x_req_valid<=1;
            fio_wp<=0;ack_end<=0;ack_wait_q<=0;all_done_reg<=1;fetch_done_reg<=1;
            wr_pend_q<=0;clr_m2m_pend_q<=0;clr_m2d_pend_q<=0;latch_pend_q<=0;full_q<=0;
            for(q=0;q<FIO_DEPTH;q=q+1) entries[q]<=0;
        end else begin
            fio_wp<=wp_next;
            for(q=0;q<FIO_DEPTH;q=q+1) entries[q]<=entries_next[q];
            all_done_reg<=(fio_done==wp_next);fetch_done_reg<=!next_fetch;
            wr_pend_q<=next_wr;clr_m2m_pend_q<=next_m2m;clr_m2d_pend_q<=next_m2d;
            latch_pend_q<=next_latch;full_q<=(pending_count>=FIO_DEPTH);
            ack_wait_q<=ack_push || (ack_wait_q && fio_done!=ack_end);
            if(ack_push) ack_end<=wp_next;
            if(exact_enable) b0_seen<=b0_toggle;
            if(ff00_write) ff00_stored<=1;
            if(issue_forward) begin
                isl_x_req_addr<=cpu_address;isl_x_req_wdata<=cpu_wdata;isl_x_req_we<=cpu_write;
                isl_x_req_port<=cpu_port;isl_x_req_exact<=0;
                isl_x_req_ioonly<=forward_ioonly && !active_badline;
                isl_x_req_seq<=~isl_x_req_seq;isl_x_req_valid<=1;
                forward_is_ack<=forward_ack;state<=FWD;
                b1_seen<=b1_toggle;irq_marker_seen<=irq_marker_q;
            end
            if(fresh_forward_reply) begin
                b1_seen<=b1_toggle;b0_seen<=b0_toggle;
                state<=b1_dma_hold?DMAWAIT:forward_is_ack?ACKWAIT:COMPLETE;
            end
            if(completion_enable) begin
                state<=RUN;isl_x_req_valid<=0;ff00_stored<=0;
            end
            // Capture the next exact request one fast edge after Enable.
            if(!turbo_active && en_q) begin
                isl_x_req_addr<=cpu_address;isl_x_req_wdata<=cpu_wdata;isl_x_req_we<=cpu_write;
                isl_x_req_port<=cpu_port;isl_x_req_exact<=1;isl_x_req_ioonly<=0;
                isl_x_req_seq<=~isl_x_req_seq;isl_x_req_valid<=1;
            end
            if(adopt) begin
                turbo_active<=b1_turbo;active_index<=b1_index;
                active_badline<=b1_badline;active_c64u_top<=b1_c64u_top;
                if(turbo_active!=b1_turbo) begin
                    state<=b1_turbo?RUN:EXACT;
                    isl_x_req_valid<=0;
                end
            end
        end
    end
    initial begin
        if(FIO_DEPTH<1 || FIO_DEPTH>8 || (FIO_DEPTH&(FIO_DEPTH-1))!=0)
            $fatal(1,"FIO_DEPTH must be 1, 2, 4 or 8");
        if(VIC_READ_GAP!=2 && VIC_READ_GAP!=3) $fatal(1,"VIC_READ_GAP must be 2 or 3");
    end
endmodule

// SPDX-License-Identifier: GPL-3.0-only
// Draft native-domain protocol. No T65 or fast-I/O access is executed here.
module multitini_c64_cpu_proxy #(
    parameter integer RESUME_PHASE=1, IRQ_SAFE_PHASE=5,
    parameter BA_LOW_IO=1
) (
    input wire clk_native, reset,
    input wire [4:0] ext_phase,
    input wire ext_frame_run, ext_cpu_enable, ext_cpu_bus_valid,
    input wire [7:0] ext_cpu_di,
    input wire ext_irq_n, ext_nmi_n, ext_rdy, ext_port_tick,
    input wire ext_dma_hold,
    input wire policy_turbo, policy_blocked, policy_badline, policy_c64u_top,
    input wire [3:0] policy_index,
    input wire pause_request, cart_present,
    input wire [15:0] isl_x_req_addr,
    input wire [7:0] isl_x_req_wdata, isl_x_req_port,
    input wire isl_x_req_we, isl_x_req_exact, isl_x_req_ioonly,
    input wire isl_x_req_seq, isl_x_req_valid,
    output reg [15:0] ext_cpu_addr=0,
    output reg [7:0] ext_cpu_do=0, ext_cpu_io=0,
    output reg ext_cpu_we=0, ext_cpu_ioonly=0,
    output reg [7:0] b0_data=0,
    output reg b0_irq_n=1, b0_nmi_n=1, b0_rdy=1,
    output reg b0_toggle=0, b0_exact=1, b0_seq=0,
    output reg b1_toggle=0, b1_dma_hold=0, b1_turbo=0,
    output reg b1_badline=1, b1_pause=0, b1_c64u_top=0,
    output reg [3:0] b1_index=0,
    output reg port_tick_toggle=0, irq_safe_toggle=0,
    output reg frame_valid=0, frame_exact=1,
    output reg done_seq=0,
    output reg diag_deadline_miss=0
);
    reg frame_seq=0;
    reg deadline_armed=0;
    wire retired=frame_valid && (frame_exact ? ext_cpu_enable : ext_cpu_bus_valid);
    always @(posedge clk_native) begin
        diag_deadline_miss<=0;
        if(reset) begin
            // Present the real reset-state CPU pins before the first slot.
            ext_cpu_addr<=isl_x_req_addr; ext_cpu_do<=isl_x_req_wdata;
            ext_cpu_we<=isl_x_req_we; ext_cpu_io<=isl_x_req_port;
            ext_cpu_ioonly<=0;
            frame_valid<=isl_x_req_valid; frame_exact<=1;
            frame_seq<=isl_x_req_seq; done_seq<=~isl_x_req_seq;
            b0_toggle<=0; b0_seq<=0; b0_exact<=1;
            b0_data<=0; b0_irq_n<=1; b0_nmi_n<=1; b0_rdy<=1;
            b1_toggle<=0; b1_dma_hold<=0; b1_turbo<=0;
            b1_badline<=1; b1_index<=0; b1_pause<=0; b1_c64u_top<=0;
            port_tick_toggle<=0; irq_safe_toggle<=0;
            deadline_armed<=0;
        end else begin
            if(ext_frame_run && ext_phase==14 && deadline_armed) begin
                if(!frame_valid && !(isl_x_req_valid && isl_x_req_exact && isl_x_req_seq!=done_seq) &&
                   (!policy_turbo || policy_blocked)) diag_deadline_miss<=1;
                deadline_armed<=0;
            end
            if(ext_port_tick) port_tick_toggle<=~port_tick_toggle;
            if(ext_frame_run && ext_phase==IRQ_SAFE_PHASE)
                irq_safe_toggle<=~irq_safe_toggle;
            if(ext_frame_run && ext_phase==RESUME_PHASE) begin
                b1_toggle<=b0_toggle;
                b1_dma_hold<=ext_dma_hold;
                b1_turbo<=policy_turbo && !policy_blocked;
                b1_badline<=policy_badline; b1_index<=policy_index;
                b1_c64u_top<=policy_c64u_top; b1_pause<=pause_request;
            end
            if(ext_frame_run && ext_phase==31) begin
                if(retired) begin
                    deadline_armed<=frame_exact;
                    b0_data<=ext_cpu_di; b0_irq_n<=ext_irq_n;
                    b0_nmi_n<=ext_nmi_n; b0_rdy<=ext_rdy;
                    b0_exact<=frame_exact; b0_seq<=frame_seq;
                    b0_toggle<=~b0_toggle; done_seq<=frame_seq;
                end
                if(!frame_exact || retired || !frame_valid) begin
                    frame_valid<=0;
                    ext_cpu_addr<=0; ext_cpu_do<=0; ext_cpu_we<=0;
                    ext_cpu_io<=isl_x_req_port; ext_cpu_ioonly<=0;
                end
            end else if(ext_frame_run && isl_x_req_valid && isl_x_req_seq!=done_seq) begin
                if((isl_x_req_exact && ext_phase>=1 && ext_phase<=14) ||
                   (!isl_x_req_exact && ext_phase==15)) begin
                    ext_cpu_addr<=isl_x_req_addr; ext_cpu_do<=isl_x_req_wdata;
                    ext_cpu_we<=isl_x_req_we; ext_cpu_io<=isl_x_req_port;
                    ext_cpu_ioonly<=BA_LOW_IO && !isl_x_req_exact &&
                                    isl_x_req_ioonly && !cart_present;
                    frame_valid<=1; frame_exact<=isl_x_req_exact;
                    frame_seq<=isl_x_req_seq;
                end
            end
        end
    end
endmodule

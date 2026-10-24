// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps
// Stage and validate a complete table before sending it to the native domain.
// The active table is retained throughout upload and switches at a SID boundary.
module multitini_c64_sid_transport #(
    parameter integer SID_COUNT=8,
    parameter integer IO_SLOTS=1, MODEL_8580=1,
    parameter bit SHARED_CORE_RUNNING=0
) (
    input wire host_clk,host_resetn,
    input wire [5:0] host_index,
    input wire [31:0] host_data,
    input wire host_write,host_commit,
    input wire [7:0] host_sequence,
    output wire [31:0] host_status,
    input wire core_clk,core_resetn,ce_1m,paused,
    input wire core_running_host,
    output wire active_valid,
    output wire [255:0] slot_masks,chip_params,
    output wire [31:0] write_inhibit,output_config,
    output wire [95:0] owners,
    output wire [127:0] io_slot_masks
);
    // A committed bank remains immutable during upload and destination reset.
    // The other bank accepts the next table. A commit swaps their roles.
    (* ram_style="block" *) reg [31:0] table_banks[0:127];
    reg write_bank=0,committed_bank=1;
    reg [31:0] table_read;
    reg read_primed=0;
    reg [56:0] written=0;
    reg [63:0] invalid=0;
    reg [4:0] entry_error[0:63];
    reg [4:0] reject_code=0;
    reg [5:0] reject_index=0;
    reg busy=0,rejected=0;
    reg [5:0] cursor=0;
    reg [7:0] commit_sequence=0;
    reg have_committed=0;
    wire core_running;
    // Production shares the controls mailbox's synchronized reset level.
    // Independent synchronizers could release related channels a cycle apart.
    if(SHARED_CORE_RUNNING)begin : shared_running
        assign core_running=core_running_host;
    end else begin : local_running
        cdc_bit_sync core_running_i(.clk(host_clk),.resetn(host_resetn),
            .din(core_resetn),.dout(core_running));
    end
    wire ready,valid;
    wire [46:0] payload;
    wire [7:0] active_sequence;
    wire native_rejected;
    wire [8:0] accepted;
    wire accepted_valid,accepted_ready;
    reg [8:0] host_accepted=0;
    function automatic [4:0] validate_word(input [5:0] index,input [31:0] data);
        begin
            validate_word=0;
            if(index<32)begin
                if(data[7:0]==0)validate_word=2;
                else if((data[7:0]>>SID_COUNT)!=0)validate_word=3;
                else if(data[31:9]!=0)validate_word=5;
                else if(data[8] && (data[7:0] & (data[7:0]-1'b1))!=0)validate_word=7;
            end else if(index<48)begin
                if(!IO_SLOTS && data[7:0]!=0)validate_word=4;
                else if((data[7:0]>>SID_COUNT)!=0)validate_word=3;
                else if(data[31:8]!=0)validate_word=5;
            end else if(index<56)begin
                if(index>=48+SID_COUNT && data!=0)validate_word=3;
                else if(data[31:18]!=0 || (!MODEL_8580 && data[17:16]!=0))validate_word=5;
            end else if(index==56)begin
                if(data[31:20]!=0 || data[19:18]==3)validate_word=5;
            end else if(data!=0)validate_word=5;
        end
    endfunction
    wire [4:0] word_error=validate_word(host_index,host_data);
    // Balanced lowest-index priority tree. Missing fields take precedence
    // over invalid data at the same index, exactly as the ordered scan did.
    wire [11:0] error_tree[1:127];
    for(genvar n=0;n<64;n=n+1)begin : error_leaf
        wire missing;
        if(n<57)assign missing=!written[n]; else assign missing=1'b0;
        assign error_tree[64+n]={missing|invalid[n],6'(n),missing?5'd1:invalid[n]?entry_error[n]:5'd0};
    end
    for(genvar n=1;n<64;n=n+1)begin : error_node
        assign error_tree[n]=error_tree[n*2][11] ? error_tree[n*2] : error_tree[n*2+1];
    end
    wire [4:0] first_error=error_tree[1][4:0];
    wire [5:0] first_index=error_tree[1][11]?error_tree[1][10:5]:6'd0;
    assign host_status={host_accepted[8],9'd0,reject_index,1'b0,reject_code,rejected,busy,host_accepted[7:0]};
    always @(posedge host_clk)begin
        if(host_write && host_index<57)table_banks[{write_bank,host_index}]<=host_data;
        table_read<=table_banks[{committed_bank,(cursor<57?cursor:6'd0)}];
    end
    wire [46:0] send_word=cursor==57 ? {1'b1,commit_sequence,6'd0,32'd0} :
        {1'b0,8'd0,cursor,table_read};
    multitini_async_mailbox #(.WIDTH(47),.DRAIN_ON_RESET(1)) table_i(
        .src_clk(host_clk),.src_resetn(host_resetn),.src_data(send_word),
        .src_valid(busy && cursor<=57 && core_running && read_primed),.src_ready(ready),
        .dst_clk(core_clk),.dst_resetn(core_resetn),.dst_data(payload),
        .dst_valid(valid),.dst_ready(1'b1));
    multitini_c64_sid_map #(.SID_COUNT(SID_COUNT)) map_i(
        .clk(core_clk),.reset(!core_resetn),.word_valid(valid && !payload[46]),
        .word_index(payload[37:32]),.word_data(payload[31:0]),
        .commit(valid && payload[46]),.commit_seq(payload[45:38]),
        .ce_1m(ce_1m),.paused(paused),.active_valid(active_valid),
        .active_seq(active_sequence),.slot_masks(slot_masks),
        .write_inhibit(write_inhibit),.owners(owners),.io_slot_masks(io_slot_masks),
        .chip_params(chip_params),.output_config(output_config),.rejected(native_rejected));
    multitini_async_mailbox #(.WIDTH(9),.DRAIN_ON_RESET(1)) accepted_i(
        .src_clk(core_clk),.src_resetn(core_resetn),.src_data({active_valid,active_sequence}),
        .src_valid(1'b1),.src_ready(accepted_ready),
        .dst_clk(host_clk),.dst_resetn(host_resetn),.dst_data(accepted),
        .dst_valid(accepted_valid),.dst_ready(1'b1));
    always @(posedge host_clk) begin
        if(!host_resetn)begin
            written<=0;invalid<=0;busy<=0;rejected<=0;cursor<=0;reject_code<=0;reject_index<=0;
            commit_sequence<=0;host_accepted<=0;have_committed<=0;
            write_bank<=0;committed_bank<=1;read_primed<=0;
        end else begin
            // Preserve the last acknowledged sequence while a destination
            // reset discards its table. Resend the immutable committed bank.
            if(!core_running && have_committed)begin busy<=1;cursor<=0;read_primed<=0;end
            else if(busy && cursor<=57 && core_running)begin
                if(ready && read_primed)begin cursor<=cursor+1'b1;read_primed<=0;end
                else read_primed<=1;
            end
            if(core_running && busy && cursor==58 && accepted_valid && accepted=={1'b1,commit_sequence})begin
                busy<=0;host_accepted<=accepted;
            end
            if(host_write)begin
                invalid[host_index]<=word_error!=0;
                entry_error[host_index]<=word_error;
                if(host_index<57)begin
                    written[host_index]<=1;
                end
            end
            if(host_commit)begin
                if(busy || host_write)begin rejected<=1;reject_code<=6;reject_index<=0;end
                else if(first_error!=0)begin rejected<=1;reject_code<=first_error;reject_index<=first_index;end
                else if(host_accepted[8] && host_sequence==host_accepted[7:0])begin
                    rejected<=1;reject_code<=5;reject_index<=0;
                end
                else begin
                    committed_bank<=write_bank;write_bank<=!write_bank;read_primed<=0;
                    busy<=1;rejected<=0;reject_code<=0;reject_index<=0;cursor<=0;commit_sequence<=host_sequence;
                    written<=0;invalid<=0;have_committed<=1;
                end
            end
        end
    end
endmodule

// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// Exact540x720 ABGR4444 artwork in external memory. Uploads pad each1080B
// row to1088B, preserving64B burst alignment without changing file contents.
// Three540-word rows are cached in dual-clock BRAM. Row descriptors publish
// only completed rows; a bank cannot be reused until scanout releases it.
module multitini_vectrex_overlay (
    input wire clk, resetn, control_clk, control_resetn, control_pixel_running,
    input wire [31:0] video_config,
    input wire pixel_clk, pixel_resetn, vblank_start,
    input wire [9:0] source_x, source_y,
    output reg [15:0] pixel_rgba,
    output reg pixel_valid,
    output reg [4:0] frame_config,
    input wire [7:0] load_data,
    input wire load_valid, load_first, load_last, load_epoch,
    input wire [31:0] load_offset, load_total,
    input wire load_abort, load_abort_epoch, transport_abort,
    output wire load_ready,
    output reg loaded, error,
    input wire capture_clk,
    inout wire [3:0] psram_a_io, psram_b_io,
    output wire psram_ce_n, psram_clk
);
    localparam integer FILE_BYTES=777600;
    localparam [4:0] DEFAULT_CONFIG=5'h0b;
    (* ram_style="block" *) reg [15:0] row_cache[0:1619];

    // Latest-value configuration CDC, including independent-reset recovery.
    wire config_ready, config_valid;
    wire [4:0] config_destination;
    reg [4:0] config_sent, config_pending;
    reg config_force;
    wire config_changed=(config_force || config_sent!=video_config[4:0]) && control_pixel_running;
    multitini_async_mailbox #(.WIDTH(5)) config_mailbox_i(
        .src_clk(control_clk),.src_resetn(control_resetn),.src_data(video_config[4:0]),
        .src_valid(config_changed),.src_ready(config_ready),
        .dst_clk(pixel_clk),.dst_resetn(pixel_resetn),.dst_data(config_destination),
        .dst_valid(config_valid),.dst_ready(1'b1));
    always @(posedge control_clk) begin
        if(!control_resetn) begin config_sent<=DEFAULT_CONFIG;config_force<=1;end
        else if(!control_pixel_running) config_force<=1;
        else if(config_changed && config_ready) begin
            config_sent<=video_config[4:0];config_force<=0;
        end
    end
    always @(posedge pixel_clk) begin
        if(!pixel_resetn) begin config_pending<=DEFAULT_CONFIG;frame_config<=DEFAULT_CONFIG;end
        else begin
            if(config_valid) config_pending<=config_destination;
            if(vblank_start) frame_config<=config_valid ? config_destination : config_pending;
        end
    end

    reg pixel_generation;
    wire generation_sync, loaded_pixel;
    cdc_bit_sync frame_sync(.clk(clk),.resetn(resetn),.din(pixel_generation),.dout(generation_sync));
    cdc_bit_sync loaded_sync(.clk(pixel_clk),.resetn(pixel_resetn),.din(loaded),.dout(loaded_pixel));
    // Consumer position is a coherent level, with frame generation attached.
    wire [10:0] consumer_value={pixel_generation,source_y<720 ? source_y : 10'd0};
    reg [10:0] consumer_sent;
    wire consumer_ready, consumer_valid;
    wire [10:0] consumer_destination;
    multitini_async_mailbox #(.WIDTH(11)) consumer_mailbox_i(
        .src_clk(pixel_clk),.src_resetn(pixel_resetn),.src_data(consumer_value),
        .src_valid(consumer_value!=consumer_sent),.src_ready(consumer_ready),
        .dst_clk(clk),.dst_resetn(resetn),.dst_data(consumer_destination),
        .dst_valid(consumer_valid),.dst_ready(1'b1));
    always @(posedge pixel_clk) begin
        if(!pixel_resetn) consumer_sent<=0;
        else if(consumer_ready && consumer_value!=consumer_sent) consumer_sent<=consumer_value;
    end

    reg fetch_generation, fetch_enabled;
    reg [9:0] consumer_row, fetch_row;
    reg [1:0] fetch_bank;
    reg [4:0] fetch_burst;
    reg [23:0] fetch_address;
    reg read_inflight, write_inflight, commit_pending;
    wire commit_ready, commit_valid;
    wire [12:0] commit_destination;
    wire [12:0] commit_source={fetch_generation,fetch_bank,fetch_row};
    multitini_async_mailbox #(.WIDTH(13)) commit_mailbox_i(
        .src_clk(clk),.src_resetn(resetn),.src_data(commit_source),
        .src_valid(commit_pending),.src_ready(commit_ready),
        .dst_clk(pixel_clk),.dst_resetn(pixel_resetn),.dst_data(commit_destination),
        .dst_valid(commit_valid),.dst_ready(1'b1));

    reg [9:0] tag_row[0:2];
    reg [2:0] tag_valid;
    reg display_armed;
    wire match0=tag_valid[0] && tag_row[0]==source_y;
    wire match1=tag_valid[1] && tag_row[1]==source_y;
    wire match2=tag_valid[2] && tag_row[2]==source_y;
    wire [10:0] cache_read_address=(match1 ? 11'd540 : match2 ? 11'd1080 : 11'd0)+{1'b0,source_x};
    always @(posedge pixel_clk) begin
        pixel_rgba<=row_cache[cache_read_address];
        pixel_valid<=display_armed && loaded_pixel && source_x<540 && source_y<720 &&
                     (match0 || match1 || match2);
        if(!pixel_resetn) begin
            pixel_generation<=0;tag_valid<=0;display_armed<=0;pixel_valid<=0;
            tag_row[0]<=0;tag_row[1]<=0;tag_row[2]<=0;
        end else begin
            if(commit_valid && commit_destination[12]==pixel_generation && commit_destination[11:10]<3) begin
                tag_row[commit_destination[11:10]]<=commit_destination[9:0];
                tag_valid[commit_destination[11:10]]<=1;
            end
            if(!loaded_pixel) begin tag_valid<=0;display_armed<=0;pixel_valid<=0;end
            if(vblank_start) begin
                pixel_generation<=!pixel_generation;tag_valid<=0;display_armed<=loaded_pixel;
            end
        end
    end

    reg upload_active, upload_epoch, aborted_epoch, aborted_valid;
    reg [19:0] expected_offset;
    reg [10:0] row_byte;
    reg [5:0] fill_count;
    reg [511:0] write_buffer;
    reg write_pending, final_write;
    reg [23:0] write_address;
    wire physical_ready, physical_done, physical_initialized, physical_fault;
    wire physical_word_valid;
    wire [15:0] physical_word;
    wire [4:0] physical_word_index;
    wire stale=(aborted_valid && load_epoch==aborted_epoch) ||
               (transport_abort && load_epoch==load_abort_epoch);
    wire beginning=load_first && load_offset==0;
    wire clear_object=load_total==1;
    wire framing_good=(load_total==FILE_BYTES || clear_object) && load_offset<load_total &&
        load_last==(load_offset+1==load_total) &&
        (beginning ? (!upload_active || upload_epoch!=load_epoch) :
            (upload_active && upload_epoch==load_epoch && load_offset=={12'd0,expected_offset} && !load_first));
    assign load_ready=resetn && physical_initialized && !write_pending && !write_inflight;
    wire transfer=load_valid && load_ready;
    wire new_upload=transfer && load_first && !stale;
    // An abort can arrive after the final data write but before firmware has
    // confirmed STREAM_END. Keep the completed object tied to its epoch.
    wire abort_upload=load_abort && (upload_active || loaded) && upload_epoch==load_abort_epoch;
    wire fetch_allowed=loaded && fetch_enabled && fetch_generation==generation_sync &&
        fetch_row<720 && {1'b0,fetch_row}<{1'b0,consumer_row}+11'd3;
    wire physical_request=!read_inflight && !write_inflight &&
        (write_pending || (fetch_allowed && !commit_pending && !new_upload && !abort_upload));
    multitini_vectrex_psram_burst psram_i(
        .clk(clk),.resetn(resetn),.capture_clk(capture_clk),
        .request_valid(physical_request),.request_ready(physical_ready),
        .request_write(write_pending),.request_addr(write_pending ? write_address : fetch_address),
        .request_wdata(write_buffer),.response_done(physical_done),
        .read_word_valid(physical_word_valid),.read_word_index(physical_word_index),.read_word(physical_word),
        .initialized(physical_initialized),.fault(physical_fault),
        .psram_a_io(psram_a_io),.psram_b_io(psram_b_io),.psram_ce_n(psram_ce_n),.psram_clk(psram_clk));
    wire [9:0] cache_column={fetch_burst,5'd0}+{5'd0,physical_word_index};
    wire [10:0] cache_write_address=(fetch_bank==1 ? 11'd540 : fetch_bank==2 ? 11'd1080 : 11'd0)+
                                   {1'b0,cache_column};
    always @(posedge clk) begin
        if(physical_word_valid && read_inflight && loaded && cache_column<540)
            row_cache[cache_write_address]<=physical_word;
    end
    always @(posedge clk) begin
        if(!resetn) begin
            loaded<=0;error<=0;upload_active<=0;upload_epoch<=0;aborted_epoch<=0;aborted_valid<=0;
            expected_offset<=0;row_byte<=0;fill_count<=0;write_buffer<=0;
            write_pending<=0;final_write<=0;write_address<=0;
            fetch_generation<=0;fetch_enabled<=0;consumer_row<=0;fetch_row<=0;fetch_bank<=0;
            fetch_burst<=0;fetch_address<=0;read_inflight<=0;write_inflight<=0;commit_pending<=0;
        end else begin
            if(consumer_valid && consumer_destination[10]==fetch_generation)
                consumer_row<=consumer_destination[9:0];
            if(physical_request && physical_ready) begin
                if(write_pending) begin write_inflight<=1;write_pending<=0;end
                else read_inflight<=1;
            end
            if(physical_done) begin
                if(write_inflight) begin
                    write_inflight<=0;write_address<=write_address+24'd64;
                    if(final_write && upload_active) begin loaded<=1;upload_active<=0;final_write<=0;end
                end
                if(read_inflight) begin
                    read_inflight<=0;
                    if(loaded) begin
                        fetch_address<=fetch_address+24'd64;
                        if(fetch_burst==16) commit_pending<=1;
                        else fetch_burst<=fetch_burst+1'b1;
                    end
                end
            end
            if(commit_pending && commit_ready) begin
                commit_pending<=0;fetch_row<=fetch_row+1'b1;fetch_burst<=0;
                fetch_bank<=fetch_bank==2 ? 0 : fetch_bank+1'b1;
            end
            if(!loaded) begin fetch_enabled<=0;commit_pending<=0;end
            if(generation_sync!=fetch_generation && !read_inflight && !write_inflight) begin
                fetch_generation<=generation_sync;fetch_enabled<=loaded;
                consumer_row<=0;fetch_row<=0;fetch_bank<=0;fetch_burst<=0;fetch_address<=0;commit_pending<=0;
            end
            if(transfer && !stale) begin
                // A new object in the alternate transport epoch retires the
                // previous abort, including clear objects. Otherwise the next
                // epoch wrap would silently discard a legitimate image.
                if(load_first) begin loaded<=0;error<=0;aborted_valid<=0;end
                if(!framing_good || (clear_object && load_data!=0)) begin
                    loaded<=0;error<=1;upload_active<=0;write_pending<=0;final_write<=0;
                end else if(clear_object) begin
                    loaded<=0;error<=0;upload_active<=0;write_pending<=0;final_write<=0;
                end else begin
                    if(beginning) begin
                        upload_active<=1;upload_epoch<=load_epoch;aborted_valid<=0;
                        write_address<=0;row_byte<=1;fill_count<=1;
                        write_buffer<=0;write_buffer[7:0]<=load_data;expected_offset<=1;
                    end else begin
                        expected_offset<=expected_offset+1'b1;
                        write_buffer[fill_count*8 +:8]<=load_data;
                        if(row_byte==1079) begin
                            write_buffer[511:448]<=0;write_pending<=1;final_write<=load_last;
                            fill_count<=0;row_byte<=0;
                        end else begin
                            row_byte<=row_byte+1'b1;
                            if(fill_count==63) begin write_pending<=1;fill_count<=0;end
                            else fill_count<=fill_count+1'b1;
                        end
                    end
                end
            end
            // Epochs advance after any aborted stream, including a stream of
            // another kind with no accepted bytes. Track all aborts for stale
            // data rejection, but invalidate artwork only for its own kind.
            if(transport_abort) begin
                aborted_valid<=1;aborted_epoch<=load_abort_epoch;
                if(abort_upload) begin
                    loaded<=0;error<=1;upload_active<=0;write_pending<=0;final_write<=0;
                end
            end
            if(physical_fault) begin loaded<=0;error<=1;upload_active<=0;write_pending<=0;end
        end
    end
endmodule

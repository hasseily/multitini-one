// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// MiSTer machine logic behind the common Multitini load/input/media services.
module multitini_apple_iigs_adapter #(
    parameter integer AUDIO_SAMPLE_HZ=48000,
    parameter integer MAX_ACCEL_SPEED=6
)(
    input wire core_clk,reset,core_reset_request,pause_request,clear_load_error,
    input wire [15:0] input_code,input_value,
    input wire input_valid,output wire input_ready,
    input wire [7:0] load_data,load_kind,
    input wire load_valid,load_first,load_last,load_epoch,load_abort,load_abort_epoch,
    input wire [31:0] load_offset,load_total,output wire load_ready,
    output wire [23:0] memory_addr,output wire memory_write,
    output wire [63:0] memory_wdata,output wire [7:0] memory_wstrb,
    output wire memory_valid,input wire memory_ready,
    input wire [63:0] memory_rdata,input wire memory_rvalid,memory_fault,
    input wire media_mount,media_eject,media_complete,
    input wire [7:0] media_slot,media_kind,
    input wire media_read_only,
    input wire [31:0] media_image_size,media_generation,
    input wire [31:0] media_complete_request_id,media_complete_status,
    input wire [15:0] media_complete_block_count,
    input wire [31:0] media_complete_generation,
    output reg media_request_valid,media_request_write,
    output reg [31:0] media_request_id,media_request_lba,
    output reg [15:0] media_request_block_count,
    output reg [31:0] media_request_generation,
    output reg [7:0] media_write_data,
    output reg media_write_data_valid,media_write_data_last,
    input wire media_write_data_ready,
    output wire media_state_mounted,media_state_dirty,media_state_eject_pending,
    output wire [31:0] core_id,output wire ready,load_error,rom_loaded,
    output wire [7:0] video_r,video_g,video_b,
    output wire video_de,video_hsync,video_vsync,
    output reg video_valid,output reg [9:0] video_x,
    output reg [8:0] video_y,output wire video_pal,
    output reg signed [23:0] audio_l,audio_r,output reg audio_valid
);
    reg [1:0] phase;
    reg clk28_divider;
    wire clk14,clk28;
    always @(posedge core_clk)begin
        if(reset)begin phase<=0;clk28_divider<=0;end
        else begin phase<=phase+1'b1;clk28_divider<=!clk28_divider;end
    end
    // Ignored experiment: SYNC gate samples CE during the low input level.
    // Native positive edges remain phase3; high pulse is one primary half-cycle.
    BUFGCE clk14_buffer_i(.I(core_clk),.CE(phase==3),.O(clk14));
    BUFG clk28_buffer_i(.I(clk28_divider),.O(clk28));
    wire pixel_ce=phase==3;
    (* KEEP="TRUE" *) wire byte_tick=phase==0;
    (* KEEP="TRUE" *) wire lookup_tick=phase==1;
    (* KEEP="TRUE" *) wire request_tick=phase==2;
    reg [10:0] key;
    reg [24:0] mouse;
    reg [15:0] mouse_low,joy,analog_xy;
    assign input_ready=!reset && byte_tick;
    always @(posedge core_clk)begin
        if(reset)begin key<=0;mouse<=0;mouse_low<=0;joy<=0;analog_xy<=16'h8080;end
        else if(input_valid && input_ready)case(input_code[15:8])
            8'h00:key<={~key[10],input_value[0],input_value[1],input_code[7:0]};
            8'h40:if(input_code[7:0]==0)mouse_low<=input_value;
                else if(input_code[7:0]==1)mouse<={input_value[8:0],mouse_low};
            8'h41:if(input_code[7:0]==0)joy<=input_value;else if(input_code[7:0]==1)analog_xy<=input_value;
            default:begin end
        endcase
    end
    wire loader_error,cache_error,config_valid,config_commit,invalidate;
    wire [127:0] config_bytes;
    wire aux_write,sector_write,nv_write;
    wire [15:0] aux_addr;
    wire [8:0] sector_addr,nv_load_addr;
    wire [7:0] aux_data,sector_data,nv_data;
    wire loader_valid,loader_ready,loader_response,loader_write;
    wire [23:0] loader_addr;
    wire [63:0] loader_data;
    wire [7:0] loader_strobe;
    multitini_apple_iigs_loader loader_i(
        .clk(core_clk),.reset(reset),.core_reset_request(core_reset_request),.clear_error(clear_load_error),
        .byte_tick(byte_tick),.load_abort(load_abort),.load_abort_epoch(load_abort_epoch),.load_epoch(load_epoch),
        .load_kind(load_kind),.load_data(load_data),.load_valid(load_valid),.load_first(load_first),.load_last(load_last),
        .load_offset(load_offset),.load_total(load_total),.load_ready(load_ready),.error(loader_error),
        .rom_loaded(rom_loaded),.config_valid(config_valid),.config_bytes(config_bytes),.config_commit(config_commit),
        .aux_write(aux_write),.aux_addr(aux_addr),.aux_data(aux_data),
        .sector_write(sector_write),.sector_addr(sector_addr),.sector_data(sector_data),
        .nv_write(nv_write),.nv_addr(nv_load_addr),.nv_data(nv_data),.invalidate(invalidate),
        .write_valid(loader_valid),.write_enable(loader_write),.write_addr(loader_addr),.write_data(loader_data),
        .write_strobe(loader_strobe),.write_ready(loader_ready),.write_response(loader_response),.memory_fault(memory_fault));
    reg [11:0] reset_count;
    reg cold;
    reg [32:0] timestamp;
    wire keyboard_reset,keyboard_cold_reset;
    always @(posedge core_clk)begin
        if(reset)begin reset_count<=12'hfff;cold<=1;timestamp<=0;end
        else begin
            if(reset_count!=0)reset_count<=reset_count-1'b1;
            if(config_commit)begin
                if(timestamp[31:0]!=config_bytes[63:32])timestamp<={~timestamp[32],config_bytes[63:32]};
                if(config_bytes[65] || config_bytes[66])begin reset_count<=12'hfff;cold<=config_bytes[66];end
            end
            if(keyboard_reset)begin reset_count<=12'hfff;cold<=keyboard_cold_reset;end
        end
    end
    wire machine_reset_raw=reset || core_reset_request || !rom_loaded || cache_error || memory_fault || reset_count!=0;
    reg native_reset_q,native_cold_q;
    // Global assertion is immediate. Publish native reset release and local
    // reset requests only on phase0, three primary periods before CLK_14M.
    always @(posedge core_clk or posedge reset)begin
        if(reset)begin native_reset_q<=1;native_cold_q<=1;end
        else if(byte_tick)begin native_reset_q<=machine_reset_raw;native_cold_q<=core_reset_request || cold;end
    end
    wire machine_reset=native_reset_q;
    wire [23:0] bus_addr;
    wire [1:0] rom_bank;
    wire [7:0] bus_write_data,bus_read_data;
    wire [63:0] bus_read_word;
    wire bus_write,fast_ce,rom_ce,phi2,cpu_valid,cache_disable,cache_stall,hdd_dma;
    wire [2:0] cpu_speed;
    wire cpu_fast_cycle,cpu_memory_cycle,cpu_local_read;
    wire [23:0] cpu_logical_addr;
    wire cpu_vector_n;
    wire [7:0] cpu_local_data,native_read_data;
    wire native_return_toggle;
    reg native_stall_q;
    // Publish the complete native hold condition on phase2. Lookup/tag/data
    // have settled since phase1; late DDR responses or control requests must
    // not change native EN after the parent samples it on phase3.
    always @(posedge core_clk)begin
        if(reset)native_stall_q<=1;
        else if(request_tick)native_stall_q<=cache_stall || pause_request || machine_reset_raw;
    end
    wire [23:0] physical_addr=rom_ce && !bus_write ?
        (config_bytes[0] ? {6'b111111,rom_bank,bus_addr[15:0]} : {7'b1111100,rom_bank[0],bus_addr[15:0]}) :
        {1'b0,bus_addr[22:0]};
    // Eight coherent asynchronous words remove the synchronous L1 lookup
    // latency from a warm instruction loop. Tags and word data are written
    // together. Raw CPU tags keep the FPI/MMU decode off this data path.
    // Every native CPU retirement invalidates: IO reads also change mapping.
    // Reset/configuration, all writers, DMA and cache-disable invalidate too.
    // Vector pulls bypass L0: exact vector-forced ROM lanes can share a raw
    // word with RAM when IOLC is inhibited. L1 keeps physical tags throughout.
    (* ram_style="distributed" *) reg [63:0] l0_data[0:7];
    reg [17:0] l0_tags[0:7];
    reg [7:0] l0_valid;
    wire [2:0] l0_index=cpu_logical_addr[5:3];
    wire l0_hit=cpu_valid && cpu_vector_n && l0_valid[l0_index] && l0_tags[l0_index]==cpu_logical_addr[23:6] &&
        !cache_disable && !hdd_dma && !bus_write && !invalidate && !loader_valid && !config_commit;
    wire access_valid=cpu_memory_cycle &&
        !machine_reset_raw && !machine_reset && !pause_request && !l0_hit;
    always @(posedge core_clk)begin
        // Retire after native_read_data was captured, not on the parent edge
        // that raises CLK14: native memory may still be reading this L0 word.
        if(reset || machine_reset_raw || machine_reset || invalidate || loader_valid || config_commit ||
           cache_disable || hdd_dma || bus_write || (cpu_ce && native_pending))
            l0_valid<=0;
        else if(access_valid && !cache_stall && cpu_vector_n)begin
            l0_data[l0_index]<=bus_read_word;
            l0_tags[l0_index]<=cpu_logical_addr[23:6];
            l0_valid[l0_index]<=1;
        end
    end
    reg native_commit_q,native_pending,native_return_seen;
    reg native_prepare_q,native_prepared;
    reg [4:0] cpu_cooldown;
    wire native_path=!cpu_fast_cycle || cpu_speed==0;
    wire native_response=native_pending && native_return_toggle!=native_return_seen;
    wire fast_response=!native_pending && !native_path &&
        (!cpu_valid || cpu_local_read || l0_hit || !cache_stall);
    wire cpu_ce=!machine_reset && !machine_reset_raw && !pause_request && !hdd_dma &&
        (native_response || (fast_response && cpu_cooldown==0));
    // A valid raw hit cannot overlap a local IO register: every mapping or
    // native IO transaction retires before the CPU can consume another hit.
    // Invalid internal cycles retain L1's floating-bus value via cpu_valid.
    wire [7:0] cpu_read_data=native_pending ? native_read_data : l0_hit ? l0_data[l0_index][cpu_logical_addr[2:0]*8+:8] :
        cpu_local_read ? cpu_local_data : bus_read_word[cpu_logical_addr[2:0]*8+:8];
    wire [7:0] native_memory_data=l0_hit ? l0_data[l0_index][cpu_logical_addr[2:0]*8+:8] : bus_read_data;
    // A native request is published one primary edge before CLK14 rises.
    // Its bus stays held until the response toggle is observed on core_clk.
    always @(posedge core_clk)begin
        if(reset || machine_reset)begin
            native_commit_q<=0;native_pending<=0;native_return_seen<=0;cpu_cooldown<=0;
            native_prepare_q<=0;native_prepared<=0;
        end else begin
            // A held native bus is identified at phase1, before its phase3
            // BRAM/HDD read-preparation edge. Publish readiness at phase0 only
            // after that edge; never commit a first-edge stale native read.
            if(lookup_tick)native_prepare_q<=native_path && !native_pending && !hdd_dma;
            if(byte_tick)begin native_commit_q<=0;native_prepared<=native_prepare_q && native_path && !native_pending;end
            if(request_tick)native_commit_q<=native_prepared && native_path && !native_pending && !hdd_dma && phi2 &&
                !cache_stall && !pause_request && !machine_reset_raw;
            if(phase==3 && native_commit_q && !hdd_dma)native_pending<=1;
            if(hdd_dma)begin native_prepare_q<=0;native_prepared<=0;end
            if(cpu_cooldown!=0)cpu_cooldown<=cpu_cooldown-1'b1;
            if(cpu_ce)begin
                native_prepare_q<=0;native_prepared<=0;
                if(native_pending)begin native_pending<=0;native_return_seen<=native_return_toggle;end
                case(cpu_speed)
                    1:cpu_cooldown<=15;
                    2:cpu_cooldown<=11;
                    3:cpu_cooldown<=7;
                    4:cpu_cooldown<=3;
                    5:cpu_cooldown<=1;
                    default:cpu_cooldown<=0;
                endcase
            end
        end
    end
    reg committed_bus;
    // Retire only after the generated CLK_14M rising edge has consumed data.
    // Clearing a completion on the parent edge that raises CLK_14M would
    // change RDY before the CPU edge (and violate its physical hold time).
    always @(posedge core_clk)begin
        if(reset)committed_bus<=0;
        else if(phase==3)committed_bus<=hdd_dma && phi2 && !native_stall_q && !machine_reset;
    end
    wire bus_commit=cpu_ce || (phase==0 && committed_bus);
    multitini_apple_iigs_cache cache_i(
        .clk(core_clk),.reset(reset),.invalidate(invalidate),
        .lookup_tick(hdd_dma ? lookup_tick : 1'b1),.request_tick(hdd_dma ? request_tick : 1'b1),
        .bus_addr(physical_addr),.bus_wdata(bus_write_data),.bus_valid(access_valid),
        // HDD DMA's sector-to-bus byte is a native registered pipeline.
        // Capture its writes only once the native PH2 data window arrives.
        .bus_write(bus_write),.bus_commit(bus_commit),.write_launch(!hdd_dma || phi2),.bypass(cache_disable),
        .bus_rdata(bus_read_data),.bus_rword(bus_read_word),.stall(cache_stall),.fault(cache_error),.hits(),.misses(),.wait_cycles(),
        .loader_valid(loader_valid),.loader_write(loader_write),.loader_addr(loader_addr),
        .loader_wdata(loader_data),.loader_wstrb(loader_strobe),.loader_ready(loader_ready),
        .loader_response(loader_response),.loader_rdata(),
        .memory_addr(memory_addr),.memory_write(memory_write),.memory_wdata(memory_wdata),.memory_wstrb(memory_wstrb),
        .memory_valid(memory_valid),.memory_ready(memory_ready),.memory_rdata(memory_rdata),
        .memory_rvalid(memory_rvalid),.memory_fault(memory_fault));
    assign core_id=32'h49494753;
    assign load_error=loader_error || cache_error || media_error;
    assign ready=rom_loaded && !loader_error && !cache_error && !memory_fault &&
        reset_count==0 && !core_reset_request && !native_reset_q;
    wire [9:0] raw_x;
    wire [8:0] raw_y;
    wire hb,vb;
    assign video_de=!hb && !vb;
    always @(posedge core_clk)begin
        if(reset)begin video_valid<=0;video_x<=0;video_y<=0;end
        else begin video_valid<=pixel_ce; if(pixel_ce)begin video_x<=raw_x;video_y<=raw_y;end end
    end
    wire [15:0] chip_audio_l,chip_audio_r;
    reg [31:0] audio_phase;
    always @(posedge core_clk)begin
        if(reset)begin audio_phase<=0;audio_valid<=0;audio_l<=0;audio_r<=0;end
        else begin
            audio_valid<=0;
            if(audio_phase>=57272728-AUDIO_SAMPLE_HZ)begin
                audio_phase<=audio_phase+AUDIO_SAMPLE_HZ-57272728;
                audio_l<=machine_reset || pause_request ? 24'd0 : {chip_audio_l,8'd0};
                audio_r<=machine_reset || pause_request ? 24'd0 : {chip_audio_r,8'd0};
                audio_valid<=1;
            end else audio_phase<=audio_phase+AUDIO_SAMPLE_HZ;
        end
    end
    // Media clients retain their pending buffers until a matching generation
    // succeeds. Failed file writes never produce the ACK falling edge.
    reg [4:0] mounted,eject_pending,sd_ack;
    reg [31:0] sizes[0:4],generations[0:4];
    reg [4:0] readonly;
    reg [1:0] hdd_mount_pulse;
    reg [4:0] mount_hold;
    reg [63:0] mount_size;
    reg mount_readonly;
    reg pending,media_error;
    reg [2:0] selected;
    reg [4:0] completion_delay,retry_delay;
    reg [8:0] send_addr;
    reg [4:0] send_delay;
    reg send_done;
    reg pram_read,pram_write;
    wire [1:0] hdd_read,hdd_write;
    wire [15:0] hdd_sector;
    wire [31:0] woz_lba[0:1];
    wire [1:0] woz_read,woz_write,woz_ready,woz_mounted,woz_busy,woz_dirty;
    wire native_eject35;
    reg [4:0] woz_reset_hold;
    always @(posedge core_clk)begin
        if(reset)woz_reset_hold<=31;
        else if(woz_reset_hold!=0)woz_reset_hold<=woz_reset_hold-1'b1;
    end
    // The divider is stopped during global reset. Keep WOZ-only reset high
    // through several native edges after its clock restarts. CPU warm reset
    // never resets controller metadata or retained dirty tracks.
    wire woz_reset=reset || woz_reset_hold!=0;
    wire [7:0] sd_data[0:4];
    wire [4:0] reads={pram_read,woz_read & {2{!woz_reset}},hdd_read};
    wire [4:0] writes={pram_write,woz_write & {2{!woz_reset}},hdd_write};
    wire mount_valid=media_slot<5 &&
        (media_slot<2 ? (media_kind==8'h40 && media_image_size!=0 && media_image_size[8:0]==0 && media_image_size<=33554432) :
         media_slot<4 ? (media_kind==8'h41 && media_image_size>=80) :
         (media_kind==8'h42 && media_image_size==1024));
    wire [8:0] sd_addr=pending && media_request_write ? send_addr : sector_addr;
    assign media_state_mounted=|mounted;
    assign media_state_dirty=(!woz_reset && |woz_dirty) || (pending && media_request_write) || media_error || pram_write;
    assign media_state_eject_pending=|eject_pending;
    integer slot;
    always @(posedge core_clk)begin
        if(reset)begin
            mounted<=0;eject_pending<=0;sd_ack<=0;readonly<=0;pending<=0;media_error<=0;
            hdd_mount_pulse<=0;mount_hold<=0;mount_size<=0;mount_readonly<=0;
            media_request_valid<=0;media_request_write<=0;media_request_id<=0;media_request_lba<=0;
            media_request_block_count<=0;media_request_generation<=0;
            media_write_data<=0;media_write_data_valid<=0;media_write_data_last<=0;
            selected<=0;completion_delay<=0;retry_delay<=0;send_addr<=0;send_delay<=0;send_done<=0;
            pram_read<=0;pram_write<=0;
            for(slot=0;slot<5;slot=slot+1)begin sizes[slot]<=0;generations[slot]<=0;end
        end else begin
            if(clear_load_error)media_error<=0;
            if(mount_hold!=0)mount_hold<=mount_hold-1'b1;else hdd_mount_pulse<=0;
            if(retry_delay!=0)retry_delay<=retry_delay-1'b1;
            if(media_mount && (!mount_valid || (pending && selected==media_slot)))media_error<=1;
            if(media_mount && mount_valid && (!pending || selected!=media_slot))begin
                mounted[media_slot]<=media_image_size!=0;readonly[media_slot]<=media_read_only;
                sizes[media_slot]<=media_image_size;generations[media_slot]<=media_generation;eject_pending[media_slot]<=0;
                if(media_slot<2)begin hdd_mount_pulse<=2'b1<<media_slot;mount_hold<=16;
                    mount_size<={32'd0,media_image_size};mount_readonly<=media_read_only;end
            end
            if(config_commit)begin
                if(config_bytes[67] && mounted[4])pram_write<=1;
                if(config_bytes[68] && mounted[4])pram_read<=1;
            end
            if(media_eject && media_slot<5)begin
                eject_pending[media_slot]<=1;
                if(media_slot==4 && mounted[4])pram_write<=1;
            end
            // The Sony command is a full CLK_14M pulse. Capture it on the
            // same phase as native track writes, and retain the image until
            // every dirty block has received a successful file-service ACK.
            // ARM keeps its file open until a later menu eject/reinsert.
            if(native_eject35_q && mounted[2])eject_pending[2]<=1;
            for(slot=0;slot<5;slot=slot+1)begin
                if(eject_pending[slot] && !(pending && selected==slot) && !reads[slot] && !writes[slot] &&
                    !(slot==2 && (woz_busy[0] || woz_dirty[0] || bit_we35)) &&
                    !(slot==3 && (woz_busy[1] || woz_dirty[1] || bit_we525)))begin
                    mounted[slot]<=0;eject_pending[slot]<=0;
                    if(slot<2)begin hdd_mount_pulse<=2'b1<<slot;mount_hold<=16;mount_size<=0;end
                end
            end
            if(!pending && retry_delay==0 && (|(reads|writes)))begin
                // Fixed priority is bounded: each request releases on ACK;
                // track seeks cannot erase another client's request.
                if((reads|writes)&5'b00001)selected<=0;
                else if((reads|writes)&5'b00010)selected<=1;
                else if((reads|writes)&5'b00100)selected<=2;
                else if((reads|writes)&5'b01000)selected<=3;
                else selected<=4;
                pending<=1;completion_delay<=0;media_request_valid<=0;send_addr<=0;send_delay<=20;
            end else if(pending && !media_request_valid && completion_delay==0 && retry_delay==0)begin
                sd_ack[selected]<=1;
                media_request_write<=writes[selected];
                media_request_lba<=selected<2 ? {16'd0,hdd_sector} : selected<4 ? woz_lba[selected-2] : {31'd0,!config_bytes[0]};
                media_request_generation<=generations[selected];
                media_request_block_count<={5'd0,selected,8'd2};media_request_id<=media_request_id+1'b1;
                media_request_valid<=1;media_write_data_valid<=0;send_addr<=0;send_delay<=20;send_done<=0;
                if(selected==4)begin pram_read<=0;pram_write<=0;end
            end
            if(pending && media_request_valid && media_request_write && !send_done && retry_delay==0)begin
                if(send_delay!=0)send_delay<=send_delay-1'b1;
                else if(!media_write_data_valid)begin
                    media_write_data<=sd_data[selected];media_write_data_valid<=1;media_write_data_last<=send_addr==511;
                end
                if(media_write_data_valid && media_write_data_ready)begin
                    media_write_data_valid<=0;
                    if(send_addr!=511)begin send_addr<=send_addr+1'b1;send_delay<=20;end
                    else send_done<=1;
                end
            end
            if(media_complete && pending && media_request_valid && media_complete_request_id==media_request_id &&
                media_complete_generation==media_request_generation &&
                (media_complete_status!=0 || media_complete_block_count==media_request_block_count))begin
                media_write_data_valid<=0;
                if(media_complete_status==0)begin media_request_valid<=0;completion_delay<=16;media_error<=0;end
                else begin
                    // Preserve the original operation/LBA and retry the same
                    // retained buffer using a fresh ID. ACK stays asserted.
                    media_request_id<=media_request_id+1'b1;media_error<=1;
                    retry_delay<=31;send_addr<=0;send_delay<=20;send_done<=0;
                end
            end
            if(completion_delay!=0)begin
                completion_delay<=completion_delay-1'b1;
                if(completion_delay==1)begin sd_ack[selected]<=0;pending<=0;retry_delay<=16;end
            end
        end
    end
    wire [7:0] track35,track35_data_in,track525_data_in;
    wire [5:0] track525;
    wire [8:0] qtrack;
    wire [15:0] bit_addr35,bit_addr525,bit_wr_addr35,bit_wr_addr525;
    wire bit_we35,bit_we525,stable_side,motor35,motor525;
    reg native_eject35_q;
    always @(posedge core_clk)begin
        native_eject35_q<=0;
        if(!reset && lookup_tick)native_eject35_q<=native_eject35;
    end
    wire [1:0] track_valid,load_complete,flux,wp;
    wire [31:0] bit_count[0:1],flux_size[0:1],flux_ticks[0:1];
    wire [7:0] bit_data[0:1];
    reg [8:0] woz_sd_addr_q;
    reg [7:0] woz_sd_data_q;
    reg [1:0] woz_sd_ack_q,woz_sd_write_q,woz_mount_q,woz_readonly_q;
    reg [31:0] woz_size_q[0:1];
    // Publish complete ARM/file-service tuples on phase0. Held loader bytes
    // are observed once at the next native edge, and all tuple fields have
    // three primary periods of setup. Twenty send-delay ticks cover this
    // publication plus the native RAM and save-data register read latency.
    always @(posedge core_clk)begin
        if(reset)begin
            woz_sd_addr_q<=0;woz_sd_data_q<=0;woz_sd_ack_q<=0;woz_sd_write_q<=0;
            woz_mount_q<=0;woz_readonly_q<=0;woz_size_q[0]<=0;woz_size_q[1]<=0;
        end else if(byte_tick)begin
            woz_sd_addr_q<=sd_addr;woz_sd_data_q<=sector_data;woz_sd_ack_q<=sd_ack[3:2];
            woz_sd_write_q<={2{sector_write}} & sd_ack[3:2];
            woz_mount_q<=mounted[3:2];woz_readonly_q<=readonly[3:2];
            woz_size_q[0]<=sizes[2];woz_size_q[1]<=sizes[3];
        end
    end
    wire [7:0] track525_id=qtrack>=2 ? qtrack[7:0]-8'd2 : 0;
    genvar drive;
    generate for(drive=0;drive<2;drive=drive+1)begin:g_woz
        // Preserve the upstream CLK_14M track RAM latency and immediate
        // lookahead. In particular, native RMW at N+1 reads the byte whose
        // address it latched at N, even when WRITE_MODE changes late.
        woz_floppy_controller #(.IS_35_INCH(drive==0),.CLOCK_MULTIPLIER(1)) floppy_i(
            .clk(clk14),.reset(woz_reset),.sd_lba(woz_lba[drive]),.sd_rd(woz_read[drive]),.sd_wr(woz_write[drive]),
            .sd_ack(woz_sd_ack_q[drive]),.sd_buff_addr(woz_sd_addr_q),.sd_buff_dout(woz_sd_data_q),.sd_buff_din(sd_data[drive+2]),
            .sd_buff_wr(woz_sd_write_q[drive]),
            .img_mounted(woz_mount_q[drive]),.img_readonly(woz_readonly_q[drive]),.img_size({32'd0,woz_size_q[drive]}),
            .track_id(drive==0 ? track35 : track525_id),.ready(woz_ready[drive]),.disk_mounted(woz_mounted[drive]),
            .busy(woz_busy[drive]),.dirty(woz_dirty[drive]),.force_flush(eject_pending[drive+2]),
            .active(drive==0 ? motor35 : motor525),.bit_count(bit_count[drive]),
            .bit_addr(drive==0 ? bit_addr35 : bit_addr525),.stable_side(drive==0 ? stable_side : 1'b0),
            .bit_data(bit_data[drive]),.bit_data_in(drive==0 ? track35_data_in : track525_data_in),
            .bit_we(drive==0 ? bit_we35 : bit_we525),.bit_wr_addr(drive==0 ? bit_wr_addr35 : bit_wr_addr525),
            .track_load_complete(load_complete[drive]),.is_flux_track(flux[drive]),
            .flux_data_size(flux_size[drive]),.flux_total_ticks(flux_ticks[drive]),
            .track_data_valid(track_valid[drive]),.disk_type_mismatch(),.disk_write_protected(wp[drive]),
            .dbg_load_sum(),.dbg_load_bytes(),.dbg_load_blocks());
    end endgenerate
    wire [8:0] nv_address=nv_write ? nv_load_addr : sd_addr;
    assign sd_data[1]=sd_data[0];
    iigs #(.RAMSIZE(128),.EXTERNAL_CPU(1),.MAX_ACCEL_SPEED(MAX_ACCEL_SPEED)) machine_i(
        .CPU_CLK(core_clk),.CPU_CE(cpu_ce),.CPU_PUBLISH(byte_tick),.CPU_DIN(cpu_read_data),.NATIVE_COMMIT(native_commit_q),
        .cpu_speed(cpu_speed),.cpu_fast_cycle(cpu_fast_cycle),.cpu_local_read(cpu_local_read),.cpu_local_data(cpu_local_data),
        .cpu_memory_cycle(cpu_memory_cycle),.cpu_logical_addr(cpu_logical_addr),.cpu_vector_n(cpu_vector_n),
        .native_read_data(native_read_data),.native_return_toggle(native_return_toggle),
        .load_rom_clk(core_clk),.load_rom_we(aux_write),.load_rom_addr(aux_addr),.load_rom_data(aux_data),
        .cpu_valid(cpu_valid),.video_x(raw_x),.video_y(raw_y),.video_pal(video_pal),
        .reset(machine_reset),.cold_reset(native_cold_q),.CLK_28M(clk28),.CLK_14M(clk14),
        .clk_vid(core_clk),.ce_pix(pixel_ce),.timestamp(timestamp),
        .R(video_r),.G(video_g),.B(video_b),.HBlank(hb),.VBlank(vb),.HS(video_hsync),.VS(video_vsync),
        .AUDIO_L(chip_audio_l),.AUDIO_R(chip_audio_r),.phi2(phi2),.phi0(),.clk_7M(),
        .top_addr(bus_addr),.rom_bankaddr(rom_bank),.top_dout(bus_write_data),.top_din(native_memory_data),
        .we(bus_write),.fastram_ce(fast_ce),.rom_ce(rom_ce),.rom_select(!config_bytes[0]),
        .ps2_key(key),.ps2_mouse(mouse),.selftest_override(config_bytes[64]),
        .host_speed(config_bytes[10:8]),.accel_capable(1'b1),.zip_regs_en(config_bytes[23:16]==0),
        .osd_spkr_delay(!config_bytes[24]),.osd_pdl_delay(!config_bytes[25]),.osd_ctr_delay(!config_bytes[26]),
        .osd_cps_follow(config_bytes[27]),.osd_irq_delay(!config_bytes[28]),.osd_twgs_gfx(1'b0),.osd_twgs_snd(1'b0),
        .twgs_present(1'b0),.accel_cfg_speed(),.accel_spkr_delay(),.accel_pdl_delay(),.accel_ctr_delay(),
        .accel_cps_follow(),.accel_irq_delay(),.accel_twgs_gfx(),.accel_twgs_snd(),
        .nv_addr(nv_address),.nv_wr(nv_write || (sector_write && sd_ack[4])),
        .nv_din(nv_write ? nv_data : sector_data),.nv_dout(sd_data[4]),
        .accel_active(),.mem_stall(native_stall_q),.dbg_hdd_dma(hdd_dma),.floppy_wp(1'b0),
        .joystick_0({16'd0,joy}),.joystick_1(32'd0),.joystick_l_analog_0(analog_xy),.joystick_l_analog_1(16'd0),
        .paddle_0(analog_xy[7:0]),.paddle_1(analog_xy[15:8]),.paddle_2(8'h80),.paddle_3(8'h80),
        .HDD_SECTOR(hdd_sector),.HDD_READ(hdd_read),.HDD_WRITE(hdd_write),.HDD_MOUNTED(hdd_mount_pulse),
        .img_readonly(mount_readonly),.img_size(mount_size),.HDD_RAM_ADDR(sd_addr),.HDD_RAM_DI(sector_data),
        .HDD_RAM_DO(sd_data[0]),.HDD_RAM_WE(sector_write && (|sd_ack[1:0])),.HDD_ACK(sd_ack[1:0]),
        .WOZ_TRACK3(track35),.WOZ_TRACK3_BIT_ADDR(bit_addr35),.WOZ_TRACK3_STABLE_SIDE(stable_side),
        .WOZ_TRACK3_BIT_DATA(bit_data[0]),.WOZ_TRACK3_BIT_COUNT(bit_count[0]),.WOZ_TRACK3_READY(woz_ready[0]),
        .WOZ_TRACK3_DATA_VALID(track_valid[0]),.WOZ_TRACK3_LOAD_COMPLETE(load_complete[0]),
        .WOZ_TRACK3_IS_FLUX(flux[0]),.WOZ_TRACK3_FLUX_SIZE(flux_size[0]),.WOZ_TRACK3_FLUX_TOTAL_TICKS(flux_ticks[0]),
        .WOZ_TRACK3_WP(wp[0] || woz_readonly_q[0]),.WOZ_TRACK3_BIT_DATA_IN(track35_data_in),.WOZ_TRACK3_BIT_WE(bit_we35),.WOZ_TRACK3_BIT_WR_ADDR(bit_wr_addr35),
        .WOZ_TRACK1(track525),.WOZ_TRACK1_QTRACK(qtrack),.WOZ_TRACK1_DATA_VALID(track_valid[1]),
        .WOZ_TRACK1_BIT_ADDR(bit_addr525),.WOZ_TRACK1_BIT_DATA(bit_data[1]),.WOZ_TRACK1_BIT_COUNT(bit_count[1]),
        .WOZ_TRACK1_LOAD_COMPLETE(load_complete[1]),.WOZ_TRACK1_IS_FLUX(flux[1]),.WOZ_TRACK1_FLUX_SIZE(flux_size[1]),
        .WOZ_TRACK1_FLUX_TOTAL_TICKS(flux_ticks[1]),.WOZ_TRACK1_WP(wp[1] || woz_readonly_q[1]),
        .WOZ_TRACK1_BIT_DATA_IN(track525_data_in),.WOZ_TRACK1_BIT_WE(bit_we525),.WOZ_TRACK1_BIT_WR_ADDR(bit_wr_addr525),
        .DISK_READY({1'b0,woz_mounted[0],1'b0,woz_mounted[1]}),.FLOPPY_WP(1'b0),
        .UART_TXD(),.UART_RXD(1'b1),.UART_RTS(),.UART_CTS(1'b0),.serial_loopback(1'b0),
        .floppy_motor_on(motor525),.floppy35_motor_on(motor35),.drive35_eject_req(native_eject35),
        .keyboard_reset(keyboard_reset),.keyboard_cold_reset(keyboard_cold_reset),.capslock(),.cache_disable(cache_disable));
endmodule

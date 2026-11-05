// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Original WOZ2 fixture and real controller. The replay case deliberately
// supplies stale block zero in response to metadata block one, matching a
// repeated transport request; no parser, metadata RAM or lookup state is forced.
module tb_apple_iigs_woz_metadata_replay;
    reg clk=0;always #5 clk=~clk;
    reg [1:0] phase=0;
    always @(posedge clk)phase<=phase+1'b1;
    integer mode=0,replay=0,flux=0,cycles=0,reads=0;
    wire native_ce=phase==3;
    wire backend_ce=(mode==1 || (mode==2 && cycles[9])) || native_ce;
    always @(posedge clk)cycles<=cycles+1;
    reg reset=1,mounted=0,ack=0,send=0,bit_we=0,force_flush=0;
    reg [8:0] addr=0;
    reg [7:0] data=0,track=0;
    wire [31:0] lba;
    wire rd,wr,ready,busy,valid,dirty,is_flux;
    wire [7:0] bit_data;
    reg [7:0] image[0:83455];
    woz_floppy_controller #(.IS_35_INCH(1)) dut(
        .clk(clk),.CLOCK_ENABLE(backend_ce),.BIT_CLOCK_ENABLE(native_ce),.WRITE_PENDING(1'b0),
        .reset(reset),.sd_lba(lba),.sd_rd(rd),.sd_wr(wr),.sd_ack(ack),
        .sd_buff_addr(addr),.sd_buff_dout(data),.sd_buff_din(),.sd_buff_wr(send && native_ce),
        .img_mounted(mounted),.img_readonly(1'b0),.img_size(flux ? 64'd2560:64'd83456),
        .track_id(track),.ready(ready),.disk_mounted(),.busy(busy),.force_flush(force_flush),.dirty(dirty),.active(1'b1),
        .bit_count(),.bit_addr(16'd0),.stable_side(track[0]),.bit_data(bit_data),.bit_data_in(8'd0),
        .bit_we(bit_we),.bit_wr_addr(16'd0),.track_load_complete(),.is_flux_track(is_flux),.flux_data_size(),
        .flux_total_ticks(),.track_data_valid(valid),.disk_type_mismatch(),.disk_write_protected(),
        .dbg_load_sum(),.dbg_load_bytes(),.dbg_load_blocks());

    reg [3:0] sony_phases=0;
    wire [6:0] sony_track;
    wire sony_at_zero;
    flux_drive sony(
        .CLK_14M(clk),.RESET(reset),.CLOCK_ENABLE(1'b1),.IS_35_INCH(1'b1),.DRIVE_ID(2'd2),
        .PHASES(sony_phases),.IMMEDIATE_PHASES(sony_phases),.LATCHED_SENSE_REG(3'd0),.IWM_MODE(5'h0f),
        .MOTOR_ON(1'b0),.SW_MOTOR_ON(1'b0),.DISKREG_SEL(1'b0),.SEL35(1'b1),.DRIVE_SELECT(1'b0),.DRIVE_SLOT(1'b0),
        .DISK_MOUNTED(1'b1),.DISK_WP(1'b0),.DOUBLE_SIDED(1'b1),.TRACK(sony_track),.AT_TRACK0_OUT(sony_at_zero),
        .TRACK_BIT_COUNT(32'd0),.TRACK_LOADED(1'b0),.TRACK_LOAD_COMPLETE(1'b0),.BRAM_DATA(8'd0),
        .IS_FLUX_TRACK(1'b0),.FLUX_DATA_SIZE(32'd0),.FLUX_TOTAL_TICKS(32'd0),.SD_TRACK_ACK(1'b0),
        .WRITE_BIT(1'b0),.WRITE_STROBE(1'b0),.WRITE_MODE(1'b0),.CHUNK_LOADED(2'd0),.CHUNK_LOADING(1'b0));

    task sony_command(input [2:0] phases);
        sony_phases={1'b0,phases};
        @(negedge clk);sony_phases={1'b1,phases};
        @(negedge clk);sony_phases={1'b0,phases};
        @(negedge clk);
    endtask

    task enabled_edges(input integer count);
        integer n;
        begin
            n=0;
            while(n<count)begin @(posedge clk);if(backend_ce)n++;end
            @(negedge clk);
        end
    endtask
    // Block service has native-byte publication and an ACK held across the
    // entire sector, followed by a gap. Fast and sparse backend CE share it.
    initial begin:service
        integer request,source;
        forever begin
            @(negedge clk);
            if(!reset && rd && !ack)begin
                request=lba;
                if(request>=(flux ? 5:163))begin
                    if(replay && request==32'h4f57 && track==32)begin
                        $display("PASS: stale WOZ header replay corrupts TRKS entry32 and requests LBA4F57 mode=%0d",mode);
                        $finish;
                    end
                    $fatal(1,"unexpected out-of-range LBA=%08x track=%0d mode=%0d replay=%0d",request,track,mode,replay);
                end
                source=(replay && request==1) ? 0:request;
                reads++;
                enabled_edges(3);ack=1;
                enabled_edges(3);
                for(integer b=0;b<512;b++)begin
                    while(phase!=0)@(negedge clk);
                    addr=b[8:0];data=image[source*512+b];send=1;
                    while(phase!=3)@(negedge clk);
                    @(posedge clk);@(negedge clk);send=0;
                end
                enabled_edges(3);ack=0;
                enabled_edges(8);
            end
        end
    end

    initial begin:test
        integer limit,reads_before;
        if($value$plusargs("MODE=%d",mode))begin end
        if($value$plusargs("REPLAY=%d",replay))begin end
        if($value$plusargs("FLUX=%d",flux))begin end
        for(integer i=0;i<83456;i++)image[i]=8'd0;
        image[0]="W";image[1]="O";image[2]="Z";image[3]="2";
        image[4]=8'hff;image[5]=8'h0a;image[6]=8'h0d;image[7]=8'h0a;
        image[12]="I";image[13]="N";image[14]="F";image[15]="O";image[16]=60;
        image[20]=2;image[21]=2;image[59]=16;
        image[80]="T";image[81]="M";image[82]="A";image[83]="P";image[84]=160;
        image[248]="T";image[249]="R";image[250]="K";image[251]="S";
        image[252]=0;image[253]=5;
        for(integer t=0;t<160;t++)begin
            image[88+t]=t[7:0];
            image[256+t*8]=t+3;
            image[258+t*8]=1;
            image[261+t*8]=16; //4096 bits.
            for(integer b=0;b<512;b++)image[(3+t)*512+b]=(t^b^8'ha5)&255;
        end
        if(flux)begin
            // Compact WOZ 2.1 fixture. All tracks share one flux payload, so
            // the parser reaches FLUX without invoking the large-TRKS skip.
            image[20]=3;image[66]=4;image[68]=1;
            image[253]=7; // 1280 bytes of descriptors and 512 bytes of flux data.
            for(integer t=0;t<160;t++)image[88+t]=8'hff;
            for(integer i=256;i<1536;i++)image[i]=0;
            image[256]=3;image[258]=1;image[261]=2;
            for(integer b=0;b<512;b++)begin image[1536+b]=8'ha5;image[2048+b]=0;end
            image[2048]="F";image[2049]="L";image[2050]="U";image[2051]="X";image[2052]=160;
        end
        repeat(16)@(negedge clk);reset=0;
        enabled_edges(8);mounted=1;
        limit=0;
        while(!(ready && valid) && limit<1000000)begin @(negedge clk);limit++;end
        if(limit==1000000)$fatal(1,"initial track did not load mode=%0d replay=%0d state=%0d",mode,replay,dut.state);
        if(!replay)begin
            for(integer i=0;i<160;i++)begin
                if(dut.meta_ram.mem[i]!== (flux ? 8'hff:i[7:0]))$fatal(1,"TMAP mismatch index=%0d",i);
                for(integer b=0;b<8;b++)
                    if(dut.meta_ram.mem[256+i*8+b]!==image[256+i*8+b])
                        $fatal(1,"TRKS mismatch index=%0d byte=%0d",i,b);
            end
        end else if({dut.meta_ram.mem[513],dut.meta_ram.mem[512]}!==16'h4f57)
            $fatal(1,"replay fixture did not produce observed ASCII metadata");
        // Seek into the first TRKS entry populated by metadata block one.
        // This is a normal physical track16 seek, with no internal forcing.
        track=32;
        enabled_edges(2);
        limit=0;
        while(!(ready && valid) && limit<1000000)begin @(negedge clk);limit++;end
        if(limit==1000000)$fatal(1,"track16 did not finish mode=%0d replay=%0d",mode,replay);
        if(replay)$fatal(1,"injected stale-header replay did not reach bad request");
        if(dut.trk_start_block_side0!==(flux ? 16'd3:16'd35) ||
           dut.trk_start_block_side1!==(flux ? 16'd3:16'd36))
            $fatal(1,"track16 selected wrong on-disk metadata");
        if(is_flux!==1'(flux))$fatal(1,"track16 did not use the requested map");

        // A direct invalid head position must neither alias to track zero nor
        // let a stray write dirty and flush the cached track's sectors.
        reads_before=reads;
        for(integer bad=160;bad<=161;bad++)begin
            track=8'(bad);bit_we=1;force_flush=1;
            enabled_edges(8);bit_we=0;
            enabled_edges(51000);
            if(reads!=reads_before || rd || wr || busy || ready || valid || dirty)
                $fatal(1,"out-of-range track %0d touched WOZ cache or storage",bad);
        end
        force_flush=0;track=32;
        enabled_edges(8);
        if(!ready || !valid || bit_data!==(flux ? 8'ha5:8'h85) || reads!=reads_before)
            $fatal(1,"out-of-range track disturbed the cached valid track");
        track=33;
        enabled_edges(8);
        if(!ready || !valid || bit_data!==(flux ? 8'ha5:8'h84) || reads!=reads_before)
            $fatal(1,"out-of-range track disturbed the other cached side");

        // Exercise the actual Sony strobe decoder in both directions, without
        // forcing the head position or relying on the WOZ lookup guard.
        for(integer cylinder=1;cylinder<80;cylinder++)begin
            sony_command(3'b001);
            if(sony_track!==7'(cylinder))$fatal(1,"Sony failed inward seek to %0d",cylinder);
        end
        repeat(11)begin
            sony_command(3'b001);
            if(sony_track!==7'd79)$fatal(1,"Sony head stepped past cylinder 79");
        end
        sony_command(3'b100);
        for(integer cylinder=78;cylinder>=0;cylinder--)begin
            sony_command(3'b001);
            if(sony_track!==7'(cylinder) || sony_at_zero!==1'b0)
                $fatal(1,"Sony failed outward seek to %0d",cylinder);
        end
        enabled_edges(86000);
        if(sony_at_zero!==1'b1)$fatal(1,"Sony home sensor did not settle at cylinder 0");
        $display("PASS: WOZ metadata, map bounds and Sony end stop mode=%0d flux=%0d reads=%0d",mode,flux,reads);
        $finish;
    end
    initial begin #30000000;$fatal(1,"metadata replay test timeout");end
endmodule

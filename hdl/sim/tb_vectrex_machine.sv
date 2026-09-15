// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// ROM inputs are supplied at run time from ignored build assets. Every image
// goes through the public loader; controls use the public input event port.
module tb_vectrex_machine #(
    parameter integer CLOCK_HZ=24000000,
    parameter integer WIDTH=135, HEIGHT=180,
    parameter integer ADDRESS_WIDTH=$clog2(WIDTH*HEIGHT)
);
    reg clk=0;
    always #5 clk=~clk;
    reg reset=1, core_reset_request=0, pause_request=1;
    reg [15:0] input_code=0, input_value=0;
    reg input_valid=0;
    wire input_ready;
    reg [7:0] load_data=0, load_kind=0;
    reg load_valid=0, load_first=0, load_last=0;
    reg [31:0] load_offset=0, load_total=0;
    wire load_ready, ready, load_error, bios_loaded, cart_loaded;
    wire [31:0] core_id, drawn_lines, overflow_count;
    wire signed [23:0] audio_l, audio_r;
    wire audio_valid;
    reg [ADDRESS_WIDTH-1:0] pixel_address=0;
    wire [7:0] pixel_intensity;
    wire [15:0] debug_cpu_address;
    wire debug_cpu_read, debug_cpu_write;
    wire [7:0] debug_cpu_data;
    wire signed [23:0] debug_beam_x, debug_beam_y;
    wire debug_beam_valid, debug_beam_blank;
    wire [7:0] debug_beam_intensity;
    multitini_vectrex_machine #(.CLOCK_HZ(CLOCK_HZ),.WIDTH(WIDTH),.HEIGHT(HEIGHT)) dut (
        .clk(clk),.reset(reset),.core_reset_request(core_reset_request),
        .pause_request(pause_request),.clear_load_error(1'b0),
        .input_code(input_code),.input_value(input_value),.input_valid(input_valid),.input_ready(input_ready),
        .load_data(load_data),.load_kind(load_kind),.load_valid(load_valid),
        .load_abort(1'b0),.load_abort_epoch(1'b0),.load_epoch(1'b0),
        .load_first(load_first),.load_last(load_last),.load_offset(load_offset),.load_total(load_total),
        .load_ready(load_ready),.ready(ready),.load_error(load_error),.bios_loaded(bios_loaded),.cart_loaded(cart_loaded),
        .core_id(core_id),.audio_l(audio_l),.audio_r(audio_r),.audio_valid(audio_valid),
        .pixel_clk(clk),.pixel_address(pixel_address),.pixel_intensity(pixel_intensity),
        .debug_cpu_address(debug_cpu_address),.debug_cpu_read(debug_cpu_read),.debug_cpu_write(debug_cpu_write),
        .debug_cpu_data(debug_cpu_data),.debug_beam_x(debug_beam_x),.debug_beam_y(debug_beam_y),
        .debug_beam_valid(debug_beam_valid),.debug_beam_intensity(debug_beam_intensity),
        .debug_beam_blank(debug_beam_blank),.drawn_lines(drawn_lines),.overflow_count(overflow_count)
    );
    reg [7:0] bios_image[0:8191];
    reg [7:0] cart_image[0:65535];
    string bios_path, cart_path, output_prefix;
    integer cart_bytes=0, run_cycles=22500000, capture_every=1500000;
    integer warm_reset_cycle=0,warm_reset_done=0;
    integer replay_controls=1, trace_start=0, trace_end=0, cpu_cycles=0;
    integer bus_reads=0, bus_writes=0, via_writes=0, ram_writes=0, cart_reads=0;
    integer beam_points=0, lit_points=0, audio_changes=0, button_reads=0;
    integer game_fetches=0,first_game_cycle=0;
    reg [15:0] first_game_pc=0;
    integer interval=0, snapshots=0, started=0;
    integer stats_file, trace_file, bus_file, audio_file;
    integer actual_button_reads=0,active_button_reads=0;
    integer dac_changes=0,dac_nonzero_samples=0;
    reg signed [7:0] previous_dac=0;
    reg [17:0] previous_controls;
    reg signed [23:0] previous_audio;
    reg [15:0] last_fetch;
    reg [31:0] fetch_hash=32'h811c9dc5;
    reg [7:0] seen_pc[0:8191];
    integer unique_pc=0;
    reg [7:0] previous_buttons=0;
    reg signed [7:0] previous_x=0, previous_y=0;

    task load_image(input [7:0] kind, input integer bytes);
        for (integer offset=0; offset<bytes; offset=offset+1) begin
            @(negedge clk);
            while (!load_ready) @(negedge clk);
            load_valid=1; load_kind=kind; load_total=bytes; load_offset=offset;
            load_first=(offset==0); load_last=(offset==bytes-1);
            load_data=(kind==8'h40) ? bios_image[offset] : cart_image[offset];
        end
        @(negedge clk); load_valid=0; load_first=0; load_last=0;
        repeat (4) @(posedge clk);
        if (load_error) $fatal(1,"Machine loader rejected kind %h",kind);
        $display("LOADED kind=%h bytes=%0d",kind,bytes);
    endtask
    task input_event(input [15:0] code, input [15:0] value);
        @(negedge clk);
        while(!input_ready)@(negedge clk);
        input_valid=1; input_code=code; input_value=value;
        @(negedge clk); input_valid=0;
    endtask
    task replay(input integer phase);
        reg [7:0] buttons;
        reg signed [7:0] axis_x,axis_y;
        begin
            // Start-button pulses during the BIOS/title, then movement and
            // fire. Repeatable controls exercise running games after launch.
            buttons=(phase%4==2) ? 8'h01 :
                    ((phase>=12 && phase%2==0) ? 8'h08 : 8'h00);
            case ((phase/2)%4)
                0: begin axis_x=80; axis_y=0; end
                1: begin axis_x=0; axis_y=80; end
                2: begin axis_x=-80; axis_y=0; end
                3: begin axis_x=0; axis_y=-80; end
            endcase
            if (phase<12) begin axis_x=0;axis_y=0;end
            if (buttons!=previous_buttons) input_event(16'h2000,{8'd0,buttons});
            if (axis_x!=previous_x) input_event(16'h2001,{axis_x,8'd0});
            if (axis_y!=previous_y) input_event(16'h2002,{axis_y,8'd0});
            previous_buttons=buttons;previous_x=axis_x;previous_y=axis_y;
        end
    endtask
    task snapshot;
        integer file_handle, nonzero;
        reg [31:0] pixel_hash;
        string path;
        begin
            @(negedge clk);pause_request=1;
            wait(dut.cpu_paused);
            repeat(64) @(posedge clk);
            wait(dut.renderer_i.state==1);
            path=$sformatf("%s_%02d.pgm",output_prefix,snapshots);
            file_handle=$fopen(path,"w");
            if (!file_handle) $fatal(1,"Cannot create snapshot %s",path);
            $fwrite(file_handle,"P2\n%0d %0d\n255\n",WIDTH,HEIGHT);
            nonzero=0;pixel_hash=32'h811c9dc5;
            for(integer p=0;p<WIDTH*HEIGHT;p=p+1) begin
                @(negedge clk);pixel_address=p;
                @(posedge clk);#1;
                if ((^pixel_intensity)===1'bx) $fatal(1,"Unknown framebuffer pixel %0d",p);
                $fwrite(file_handle,"%0d\n",pixel_intensity);
                if(pixel_intensity!=0)nonzero=nonzero+1;
                pixel_hash=(pixel_hash^pixel_intensity)*32'h01000193;
            end
            $fclose(file_handle);
            $fwrite(stats_file,"SNAPSHOT,%0d,%0d,%0d,%08x,%0d,%0d\n",snapshots,cpu_cycles,nonzero,pixel_hash,drawn_lines,overflow_count);
            $display("SNAPSHOT %0d cycles=%0d pc=%h pixels=%0d lines=%0d overflow=%0d loop=%02x%02x buttons=%02x",
                snapshots,cpu_cycles,last_fetch,nonzero,drawn_lines,overflow_count,dut.ram['h25],dut.ram['h26],dut.ram['h0f]);
            $fflush(stats_file);
            snapshots=snapshots+1;
            @(negedge clk);pause_request=0;
        end
    endtask

    always @(posedge clk) if(started) begin
        if(dut.cpu_tick) cpu_cycles=cpu_cycles+1;
        if(debug_cpu_read)bus_reads=bus_reads+1;
        if(debug_cpu_write)bus_writes=bus_writes+1;
        if(debug_cpu_read && debug_cpu_address<16'h8000)cart_reads=cart_reads+1;
        if(debug_cpu_write && debug_cpu_address>=16'hc800 && debug_cpu_address<16'hd000)ram_writes=ram_writes+1;
        if(debug_cpu_write && debug_cpu_address>=16'hd000 && debug_cpu_address<16'hd800)via_writes=via_writes+1;
        if(debug_cpu_read && debug_cpu_address==16'hd001 && dut.psg_i.addr==14)button_reads=button_reads+1;
        if(debug_cpu_read && debug_cpu_address==16'hd001 && dut.psg_i.addr==14 &&
            dut.pa_ddr==0 && !dut.pb_bus[4] && dut.pb_bus[3])begin
            actual_button_reads=actual_button_reads+1;
            if(dut.cpu_data_in!=8'hff)active_button_reads=active_button_reads+1;
        end
        if(debug_cpu_read && dut.instruction_fetch)begin
            last_fetch=debug_cpu_address;
            if((cart_bytes!=0 && debug_cpu_address<16'h8000) ||
               (cart_bytes==0 && debug_cpu_address>=16'he000 && debug_cpu_address<16'hf000))begin
                if(game_fetches==0)begin first_game_pc=debug_cpu_address;first_game_cycle=cpu_cycles;end
                game_fetches=game_fetches+1;
            end
            if(!seen_pc[debug_cpu_address>>3][debug_cpu_address&7])begin
                seen_pc[debug_cpu_address>>3][debug_cpu_address&7]=1;
                unique_pc=unique_pc+1;
            end
            fetch_hash=(fetch_hash^debug_cpu_address)*32'h01000193;
        end
        if(debug_beam_valid) begin
            beam_points=beam_points+1;
            if(!debug_beam_blank && debug_beam_intensity!=0)lit_points=lit_points+1;
            if(trace_file && cpu_cycles>=trace_start && cpu_cycles<trace_end)
                $fwrite(trace_file,"%0d,%0d,%0d,%0d,%0d\n",cpu_cycles,debug_beam_x,debug_beam_y,debug_beam_intensity,debug_beam_blank);
        end
        if(audio_valid) begin
            if(audio_l!==previous_audio)audio_changes=audio_changes+1;
            previous_audio=audio_l;
            if(!pause_request && ready)
                $fwrite(audio_file,"%c%c%c%c%c%c",audio_l[7:0],audio_l[15:8],audio_l[23:16],
                    audio_r[7:0],audio_r[15:8],audio_r[23:16]);
            if(!pause_request && ready && dut.dac_audio!=0)dac_nonzero_samples=dac_nonzero_samples+1;
        end
        if(dut.dac_audio!==previous_dac)dac_changes=dac_changes+1;
        previous_dac=dut.dac_audio;
        if(bus_file && cpu_cycles>=trace_start && cpu_cycles<trace_end &&
            previous_controls!={dut.ca2,dut.cb2,dut.pb_bus,dut.pa_bus})begin
            $fwrite(bus_file,"%0d,%0t,%02x,%02x,%0d,%0d\n",cpu_cycles,$time,dut.pa_bus,dut.pb_bus,dut.ca2,dut.cb2);
            previous_controls={dut.ca2,dut.cb2,dut.pb_bus,dut.pa_bus};
        end
        if(dut.cpu_tick && ((^debug_cpu_address)===1'bx || (^dut.cpu_data_in)===1'bx))
            $fatal(1,"Unknown CPU bus at cycle %0d address=%h data=%h",cpu_cycles,debug_cpu_address,dut.cpu_data_in);
    end

    initial begin
        if(!$value$plusargs("BIOS=%s",bios_path))$fatal(1,"+BIOS=path required");
        if(!$value$plusargs("OUT=%s",output_prefix))output_prefix="vectrex_game";
        if($value$plusargs("CART=%s",cart_path))begin end
        if($value$plusargs("CART_BYTES=%d",cart_bytes))begin end
        if($value$plusargs("CYCLES=%d",run_cycles))begin end
        if($value$plusargs("CAPTURE_EVERY=%d",capture_every))begin end
        if($value$plusargs("CONTROLS=%d",replay_controls))begin end
        if($value$plusargs("WARM_RESET_CYCLE=%d",warm_reset_cycle))begin end
        if($value$plusargs("TRACE_START=%d",trace_start))begin end
        if($value$plusargs("TRACE_END=%d",trace_end))begin end
        for(integer n=0;n<8192;n=n+1)seen_pc[n]=0;
        $readmemh(bios_path,bios_image);
        if(cart_bytes!=0)$readmemh(cart_path,cart_image,0,cart_bytes-1);
        stats_file=$fopen({output_prefix,"_stats.csv"},"w");
        if(!stats_file)$fatal(1,"Cannot create statistics output");
        audio_file=$fopen({output_prefix,"_audio.s24le"},"wb");
        if(!audio_file)$fatal(1,"Cannot create PCM audio output");
        if(trace_end>trace_start)begin
            trace_file=$fopen({output_prefix,"_beam.csv"},"w");
            bus_file=$fopen({output_prefix,"_pins.csv"},"w");
        end else begin trace_file=0;bus_file=0;end
        repeat(8)@(posedge clk);
        @(negedge clk);reset=0;
        load_image(8'h40,8192);
        if(cart_bytes!=0)load_image(8'h41,cart_bytes);
        wait(ready);
        $display("READY bios=%b cart=%b",bios_loaded,cart_loaded);
        @(negedge clk);pause_request=0;started=1;
        while(cpu_cycles<run_cycles)begin
            wait(cpu_cycles>=(interval+1)*375000 || cpu_cycles>=run_cycles);
            interval=interval+1;
            if(warm_reset_cycle>0 && cpu_cycles>=warm_reset_cycle && !warm_reset_done)begin
                // Exercise the physical warm-reset path after the BIOS has
                // established its RAM flag. No ROM or RAM is patched.
                @(negedge clk);core_reset_request=1;
                repeat(8)@(negedge clk);
                core_reset_request=0;
                wait(ready);warm_reset_done=1;
                $display("WARM_RESET cycles=%0d",cpu_cycles);
            end
            if(replay_controls)replay(interval);
            if(capture_every>0 && cpu_cycles>=snapshots*capture_every+capture_every)snapshot();
        end
        snapshot();
        @(negedge clk);pause_request=1;started=0;
        $fwrite(stats_file,"TOTAL,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%08x\n",
            cpu_cycles,bus_reads,bus_writes,via_writes,ram_writes,cart_reads,beam_points,lit_points,audio_changes,button_reads,unique_pc,overflow_count,fetch_hash);
        $fwrite(stats_file,"GAME,%0d,%0d,%04x,%0d,%0d\n",game_fetches,first_game_cycle,first_game_pc,
            actual_button_reads,active_button_reads);
        $fwrite(stats_file,"DAC,%0d,%0d\n",dac_changes,dac_nonzero_samples);
        $fclose(stats_file);
        $fclose(audio_file);
        if(trace_file)$fclose(trace_file);
        if(bus_file)$fclose(bus_file);
        if(cpu_cycles<run_cycles || via_writes<100 || lit_points<100 || unique_pc<100)
            $fatal(1,"Insufficient machine activity cycles=%0d VIA=%0d lit=%0d PC=%0d",cpu_cycles,via_writes,lit_points,unique_pc);
        $display("PASS: Vectrex machine executed cycles=%0d VIA=%0d lit=%0d PCs=%0d overflow=%0d",cpu_cycles,via_writes,lit_points,unique_pc,overflow_count);
        $finish;
    end
    initial begin #20000000000; $fatal(1,"Machine acceptance timeout"); end
endmodule

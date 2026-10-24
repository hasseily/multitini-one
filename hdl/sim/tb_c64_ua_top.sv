// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_c64_ua_top #(parameter integer STRESS=0, YIELD=0);
    reg clk=0;always #5 clk=~clk;
    reg hard_reset=1,guest_reset=0,enable=1,paused=0,hold=0,reg_write=0;
    reg [7:0] reg_addr=0,reg_wdata=0;wire [7:0] reg_rdata;
    reg diagnostics_armed=0,audio_tick=0,board_clip=0;
    reg [83:0] cache_pending_addr=0;reg [3:0] cache_pending_valid=0;
    reg [20:0] cache_inflight_write_addr=0,cache_write_done_addr=0,cache_write_snoop_addr=0;
    reg cache_inflight_write_valid=0,cache_write_done=0,cache_write_snoop_valid=0,reu_memory_busy=0;
    wire irq,held;wire [31:0] status,fetch_count;wire signed [17:0] audio_l,audio_r;
    wire [23:0] memory_addr;wire memory_valid,memory_busy;
    reg memory_ready=1;reg [63:0] memory_rdata=0;reg memory_rvalid=0,memory_fault=0;
    multitini_c64_ua #(.UA_YIELD_TO_REU(YIELD)) dut(.*);
    integer cycle=0,latency=64,delay_count=0,mode,rate_value,offset,v,i,j,l,out,cases=0,waits;
    reg backend_owed=0,checking=0;
    reg shared_trace=0;integer shared_log;
    integer voices_count=7,scenario_kind=0,last_issue=-100,min_delay=999,max_delay=0;
    reg [23:0] outstanding_addr;
    reg [15:0] expected[0:6],held_sample[0:6];
    reg [6:0] boundary,ready_before;
    integer first_misses[0:6],mid_misses[0:6],boundaries[0:6];
    function automatic [7:0] byte_at(input [23:0] address);begin byte_at=address[7:0]^address[15:8]^address[23:16]^8'h97;end endfunction
    function automatic [15:0] sample_at(input [23:0] address,input bit wide);
        begin sample_at=wide ? {byte_at(address+24'd1),byte_at(address)} : {byte_at(address),8'b0};end
    endfunction
    genvar g;
    generate for(g=0;g<7;g=g+1)begin: check_voice
        always @(posedge clk)begin
            boundary[g]=checking && dut.voices[g].voice.boundary && !paused;
            ready_before[g]=dut.sample_ready[g];held_sample[g]=dut.samples[g*16+:16];
            if(boundary[g] && ready_before[g] && dut.next_samples[g*16+:16]!==expected[g])
                $fatal(1,"Cross-voice or stale sample v%0d expected %04h got %04h",g,expected[g],dut.next_samples[g*16+:16]);
            if(dut.fetch[g]) expected[g]=sample_at(dut.fetch_address[g*24+:24],dut.sixteen_bit[g]);
            if(shared_trace && boundary[g])$fdisplay(shared_log,"%0d,boundary,%0d,%0d",cycle,g,ready_before[g]);
            #1;
            if(boundary[g])begin
                if(!ready_before[g])begin
                    if(dut.samples[g*16+:16]!==held_sample[g])$fatal(1,"Underrun did not hold previous output");
                    if(boundaries[g]==0)first_misses[g]=first_misses[g]+1;else mid_misses[g]=mid_misses[g]+1;
                end
                boundaries[g]=boundaries[g]+1;
            end
        end
    end endgenerate
    always @(posedge clk)begin
        cycle=cycle+1;
        if(memory_valid && memory_ready && !hard_reset)begin
            if(backend_owed)$fatal(1,"Multiple HP3 reads");
            if(cycle-last_issue<32)$fatal(1,"Native issue cap violated");
            last_issue=cycle;
            outstanding_addr=memory_addr;backend_owed=1;
            delay_count=latency>0 ? latency-1 : 9+((cycle*73)%191);
            if(delay_count+1<min_delay)min_delay=delay_count+1;
            if(delay_count+1>max_delay)max_delay=delay_count+1;
            if(shared_trace)$fdisplay(shared_log,"%0d,issue,%0d,%h",cycle,dut.fetcher.turn_request,memory_addr);
        end
    end
    always @(negedge clk)begin
        audio_tick=(cycle%657)==0;
        if(YIELD)reu_memory_busy=(cycle%256<64)||(cycle%1024>=128 && cycle%1024<192);
        memory_rvalid=0;
        if(backend_owed)begin
            if(delay_count==0)begin
                memory_rvalid=1;for(integer b=0;b<8;b=b+1)memory_rdata[b*8+:8]=byte_at(outstanding_addr+b);
                backend_owed=0;
            end else delay_count=delay_count-1;
        end
    end
    task step;begin @(posedge clk);#2;@(negedge clk);#2;end endtask
    task write_reg(input integer voice,input [4:0] offset,input [7:0] value);
        begin reg_addr=(voice+1)*32+offset;reg_wdata=value;reg_write=1;step();reg_write=0;step();end
    endtask
    task write24(input integer voice,input [4:0] offset,input [23:0] value);
        begin write_reg(voice,offset,value[23:16]);write_reg(voice,offset+1,value[15:8]);write_reg(voice,offset+2,value[7:0]);end
    endtask
    task fresh;
        begin
            checking=0;guest_reset=1;hold=0;enable=1;paused=0;reg_write=0;
            repeat(2)step();while(memory_busy)step();guest_reset=0;
            for(integer n=0;n<7;n=n+1)begin first_misses[n]=0;mid_misses[n]=0;boundaries[n]=0;expected[n]=0;end
        end
    endtask
    reg [23:0] base;
    integer first_total,mid_total;
    task stream_case(input integer count,input integer selected_rate,input integer selected_mode,
                     input integer selected_offset,input integer duration,input bit aligned,input bit loop_jump);
        begin
            fresh();rate_value=selected_rate;mode=selected_mode;offset=selected_offset;voices_count=count;
            for(v=0;v<count;v=v+1)begin
                base=(v%3==0)?24'h000000:((v%3==1)?24'h7ff000:24'hfff000);base=base+v*128+offset;
                write24(v,5,base);write24(v,9,4096);write24(v,17,loop_jump?16:0);write24(v,21,loop_jump?80:4096);
                write_reg(v,14,rate_value>>8);write_reg(v,15,rate_value);
            end
            checking=1;
            if(aligned)begin
                for(v=0;v<count;v=v+1)dut.control[v]=3 | (mode==0?0:(mode==1?16:80));
            end else for(v=0;v<count;v=v+1)begin
                write_reg(v,0,3 | (mode==0?0:(mode==1?16:80)));repeat(30)step();
            end
            repeat(duration)step();checking=0;first_total=0;mid_total=0;
            for(v=0;v<count;v=v+1)begin first_total+=first_misses[v];mid_total+=mid_misses[v];end
            $fdisplay(out,"%0d,%0d,%0d,%0d,%0d,%0d,%0d",latency,rate_value,mode,offset,first_total,mid_total,boundaries[0]);
            if(!YIELD && count==2 && rate_value==130 && (first_total!=0 || mid_total!=0))$fatal(1,"Qualified two-voice random latency underrun");
            if(loop_jump && rate_value==130 && (!YIELD || offset==0) && mid_total!=0)$fatal(1,"Qualified aligned-loop underrun yield%0d mode%0d offset%0d count%0d",YIELD,mode,offset,mid_total);
            cases=cases+1;
        end
    endtask
    initial begin
        out=$fopen("aggregate.csv","w");$fdisplay(out,"latency,rate,mode,offset,first,mid,boundaries");
        shared_log=$fopen("shared_trace.csv","w");
        repeat(2)step();hard_reset=0;fresh();
        if(STRESS)begin
            // Fixed bounded service, phase-aligned loop discontinuities.
            for(l=0;l<2;l=l+1)for(j=0;j<2;j=j+1)for(i=0;i<3;i=i+1)begin
                latency=l?90:64;
                stream_case(7,j?130:61,i,0,100000,1,1);
                stream_case(7,j?130:61,i,7,100000,1,1);
            end
            if(!YIELD)begin
                // Variable acknowledged read latency10..200 across all byte
                // modes, aperture regions and both accepted voice counts.
                latency=0;min_delay=999;max_delay=0;
                for(l=0;l<2;l=l+1)for(j=0;j<4;j=j+1)for(i=0;i<3;i=i+1)begin
                    rate_value=(j==3)?130:(4+j*8);
                    stream_case(l?7:2,rate_value,i,0,60000,0,0);
                    stream_case(l?7:2,rate_value,i,7,60000,0,0);
                end
                // One complete4KiB stream at the qualified two-voice rate.
                stream_case(2,130,0,0,2800000,0,0);
                if(min_delay!=10 || max_delay!=200)$fatal(1,"Random latency range not exercised");
            end
            $fclose(out);$fclose(shared_log);
            $display("PASS: UA aggregate stress yield=%0d cases=%0d latency_min=%0d latency_max=%0d",YIELD,cases,min_delay,max_delay);$finish;
        end
        // Full read map and byte-immediate register assembly.
        for(i=32;i<256;i=i+1)begin reg_addr=i;#1;if(reg_rdata!==(i%2 ? 8'h10 : 8'h00))$fatal(1,"Read map %h",i);end
        write24(6,5,24'h123456);write_reg(6,4,8'hff);write_reg(6,1,8'hff);write_reg(6,2,8'hff);
        if(dut.start_address[6]!=24'h123456 || dut.volume[6]!=63 || dut.pan[6]!=15)$fatal(1,"Register assembly/masks");
        // Even when voices play, all diagnostic fields must remain hidden.
        write_reg(0,15,1);write_reg(0,0,1);repeat(40)step();
        if(status!=0 || fetch_count!=0)$fatal(1,"Disarmed diagnostic leakage");
        diagnostics_armed=1;repeat(4)step();if(status[14:8]!=1)$fatal(1,"Playing diagnostic missing");
        board_clip=1;step();board_clip=0;guest_reset=1;step();guest_reset=0;
        if(!status[2])$fatal(1,"Guest reset cleared diagnostic history");
        diagnostics_armed=0;step();if(status!=0 || fetch_count!=0)$fatal(1,"Disarm did not clear diagnostics");
        diagnostics_armed=1;step();step();
        // One-byte one-shot sets its IRQ; clear is per voice or global.
        fresh();write_reg(0,15,1);write24(0,9,1);write24(0,21,1);write_reg(0,0,5);
        waits=0;while(!irq && waits<100)begin step();waits=waits+1;end
        if(!irq)$fatal(1,"One-shot IRQ missing");reg_addr=8'hfe;#1;if(reg_rdata!=1)$fatal(1,"IRQ status map");
        write_reg(0,31,1);if(irq)$fatal(1,"Per-voice clear failed");
        force dut.irq_set=7'h40;reg_addr=8'hff;reg_wdata=8'hff;reg_write=1;step();reg_write=0;release dut.irq_set;
        if(dut.irq_flags!=7'h40)$fatal(1,"Set did not beat clear");
        write_reg(0,31,8'hff);if(irq)$fatal(1,"Global clear failed");
        // All seven real voices, register-driven gate spacing of one phi2.
        for(l=0;l<2;l=l+1)for(j=0;j<2;j=j+1)for(mode=0;mode<3;mode=mode+1)for(offset=0;offset<8;offset=offset+1)begin
            if(offset!=0 && offset%2==0)begin end
            else begin
                fresh();latency=l ? 90 : 64;rate_value=j ? 130 : 61;
                for(v=0;v<7;v=v+1)begin
                    base=(v%3==0)?24'h000000:((v%3==1)?24'h7ff000:24'hfff000);base=base+v*128+offset;
                    write24(v,5,base);write24(v,9,1024);write24(v,17,0);write24(v,21,1024);
                    write_reg(v,14,rate_value>>8);write_reg(v,15,rate_value);write_reg(v,1,32);write_reg(v,2,8);
                end
                checking=1;
                for(v=0;v<7;v=v+1)begin
                    write_reg(v,0,3 | (mode==0 ? 0 : (mode==1 ? 16 : 80)));repeat(30)step();
                end
                repeat(18000)step();checking=0;first_total=0;mid_total=0;
                for(v=0;v<7;v=v+1)begin first_total=first_total+first_misses[v];mid_total=mid_total+mid_misses[v];end
                $fdisplay(out,"%0d,%0d,%0d,%0d,%0d,%0d,%0d",latency,rate_value,mode,offset,first_total,mid_total,boundaries[0]);
                if(rate_value==130 && mid_total!=0)$fatal(1,"48kHz midstream underrun latency=%0d mode=%0d offset=%0d count=%0d",latency,mode,offset,mid_total);
                if(rate_value==130 && (mode==0 || offset!=7) && first_total!=0)$fatal(1,"Unexpected first-sample underrun");
                if(rate_value==61 && latency==64 && offset==0 && (first_total!=0 || mid_total!=0))$fatal(1,"Qualified aligned rate61 underrun");
                cases=cases+1;
            end
        end
        // Aligned one-sample loops all share one word. A write immediately
        // after a boundary preserves its already latched sample, invalidates
        // the resident word and defers all seven predictors for 8.5 us.
        for(l=0;l<2;l=l+1)for(j=0;j<2;j=j+1)begin
            fresh();latency=l ? 90 : 64;rate_value=j ? 130 : 61;
            for(v=0;v<7;v=v+1)begin
                write24(v,5,24'h3000);write24(v,9,1);write24(v,17,0);write24(v,21,1);
                write_reg(v,14,rate_value>>8);write_reg(v,15,rate_value);
            end
            // Directed setup aligns the seven counters; CPU register writes
            // are separately covered by all60 normal gate-on scenarios.
            for(v=0;v<7;v=v+1)dut.control[v]=3;
            checking=1;repeat(2500)step();
            while(!dut.voices[0].voice.boundary)step();step();
            shared_trace=1;$fdisplay(shared_log,"%0d,start,%0d,%0d",cycle,latency,rate_value);
            for(v=0;v<7;v=v+1)begin first_misses[v]=0;mid_misses[v]=0;end
            cache_pending_addr[20:0]=21'h600;cache_pending_valid=1;
            cache_write_snoop_addr=21'h600;cache_write_snoop_valid=1;step();cache_write_snoop_valid=0;
            repeat(267)step();cache_pending_valid=0;$fdisplay(shared_log,"%0d,release,0,0",cycle);
            repeat(4000)step();checking=0;mid_total=0;
            for(v=0;v<7;v=v+1)mid_total=mid_total+mid_misses[v];
            $fdisplay(out,"%0d,%0d,shared-defer,0,0,%0d,%0d",latency,rate_value,mid_total,boundaries[0]);
            if(rate_value==130 && mid_total!=0)$fatal(1,"Shared defer at rate130 count%0d",mid_total);
            if(rate_value==61 && mid_total>(latency==64?2:3))begin $fclose(shared_log);$fatal(1,"Shared defer exceeds model latency%0d count%0d",latency,mid_total);end
            shared_trace=0;
        end
        // Hold applies reset immediately and discards old reads after release.
        hold=1;step();if(!held || dut.playing || irq || audio_l || audio_r)$fatal(1,"Hold not applied");
        while(memory_busy)step();hold=0;step();if(held || dut.playing)$fatal(1,"Hold release restarted voices");
        memory_fault=1;step();memory_fault=0;guest_reset=1;step();guest_reset=0;
        if(!status[0] || audio_l || audio_r || memory_valid)$fatal(1,"Fault local mute/lifetime");
        $fclose(out);$display("PASS: UA top registers=224 aggregate_cases=%0d",cases);$finish;
    end
    initial begin #150000000;$fatal(1,"UA top timeout");end
endmodule
module tb_c64_ua_stress;tb_c64_ua_top #(.STRESS(1)) test();endmodule
module tb_c64_ua_yield;tb_c64_ua_top #(.STRESS(1),.YIELD(1)) test();endmodule

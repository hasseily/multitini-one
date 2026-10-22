// SPDX-License-Identifier: GPL-3.0-only
// Included inside the production adapter bench: real CPU, cache, MMU and DMA.
    task smart_long_load(input [23:0] address);begin emit(8'haf);emit(address[7:0]);emit(address[15:8]);emit(address[23:16]);end endtask
    task smart_long_store(input [23:0] address);begin emit(8'h8f);emit(address[7:0]);emit(address[15:8]);emit(address[23:16]);end endtask
    task smart_command(input integer unit,input [7:0] command,input [23:0] buffer,input [31:0] block_number);begin
        immediate(command);sta(16'hc0f2);immediate(unit);sta(16'hc0f3);
        immediate(buffer[7:0]);sta(16'hc0f4);immediate(buffer[15:8]);sta(16'hc0f5);
        immediate(buffer[23:16]);sta(16'hc0f8);
        immediate(block_number[7:0]);sta(16'hc0f6);immediate(block_number[15:8]);sta(16'hc0f7);
        immediate(block_number[23:16]);sta(16'hc0f9);immediate(block_number[31:24]);sta(16'hc0fa);
        lda(16'hc0f0);lda(16'hc0f1);
    end endtask
    task smart_packet(input integer unit);reg [7:0] value;begin
        load_epoch=~load_epoch;
        for(integer i=0;i<512;i=i+1)begin
            value=(i*7+19+unit*29)&255;
            @(negedge core_clk);load_kind=8'h48;load_total=512;load_offset=i;load_data=value;
            load_first=i==0;load_last=i==511;load_valid=1;
            while(!load_ready)@(negedge core_clk);
            @(negedge core_clk);load_valid=0;
        end
        repeat(40)@(negedge core_clk);
        if(load_error)$fatal(1,"SmartPort sector packet failed");
    end endtask
    task smart_wait_media(input integer unit,input bit writing);integer polls,wire_slot;begin
        polls=0;wire_slot=unit<2 ? unit : unit+3;
        while(!media_request_valid && polls<3000000)begin @(negedge core_clk);polls=polls+1;end
        if(!media_request_valid || media_request_write!=writing || media_request_lba!=32'h80000001+unit ||
            media_request_block_count!==(wire_slot*256+2) || media_request_generation!=32'h100+unit)
            $fatal(1,"SmartPort wire mapping unit%0d write%b lba%x blockcount%x generation%x PC%x",unit,media_request_write,
                media_request_lba,media_request_block_count,media_request_generation,dut.machine_i.cpu_addr);
    end endtask
    task smart_receive(input integer unit);integer polls;begin
        for(integer i=0;i<512;i=i+1)begin
            polls=0;while(!media_write_data_valid && polls<10000)begin @(negedge core_clk);polls=polls+1;end
            if(!media_write_data_valid || media_write_data!==((i*7+19+unit*29)&255) || media_write_data_last!=(i==511))
                $fatal(1,"SmartPort write unit%0d byte%0d got%x",unit,i,media_write_data);
            media_write_data_ready=1;@(negedge core_clk);media_write_data_ready=0;
        end
    end endtask
    task smartport_machine_checks(input integer speed,input bit bypass);
        reg [23:0] buffer;integer wire_slot;
        begin
            @(negedge core_clk);reset=1;core_reset_request=1;repeat(20)@(negedge core_clk);reset=0;
            build_program(0);program_cursor=122880;
            emit(8'h78);immediate(8'h80);sta(16'hc036);immediate(8'h3f);sta(16'hc035);
            immediate(8'h80);sta(16'hc02d);sta(16'hc006);
            if(bypass)begin immediate(8'h50);repeat(4)sta(16'hc05a);immediate(8'hf7);sta(16'hc059);end
            for(integer unit=0;unit<8;unit=unit+1)begin
                buffer=24'h02ff80+unit*512;
                // Warm both cache levels before DMA overwrites these bytes.
                smart_long_load(buffer);smart_long_load(buffer+511);
                smart_command(unit+1,8'hc1,buffer,32'h80000001+unit);
                smart_long_store(24'h020100+unit*4);
                smart_long_load(buffer);smart_long_store(24'h020101+unit*4);
                smart_long_load(buffer+511);smart_long_store(24'h020102+unit*4);
                smart_command(unit+1,8'hc2,buffer,32'h80000001+unit);
                smart_long_store(24'h020103+unit*4);
            end
            smart_command(1,8'hc1,24'h050000,32'h80000001);
            smart_long_store(24'h020150);smart_long_load(24'h050000);smart_long_store(24'h020151);
            lda(16'hc700);smart_long_store(24'h020140);
            lda(16'hc800);smart_long_store(24'h020141);
            lda(16'hcfff);smart_long_store(24'h020142);
            lda(16'hc800);smart_long_store(24'h020143); // CFFF leaves no owner.
            lda(16'hc700);lda(16'hc600);lda(16'hc800);smart_long_store(24'h020144);
            emit(8'hdb);
            for(integer i=0;i<8;i=i+1)begin
                memory[(24'h02ff80+i*512)>>3]=0;memory[(24'h03017f+i*512)>>3]=0;
                memory[(24'h020100+i*4)>>3]=0;
            end
            memory[24'h050000>>3]=64'haaaaaaaaaaaaaaaa;
            packet(8'h43,16,speed);packet(8'h41,131072,0);packet(8'h42,4096,0);packet(8'h46,4096,0);packet(8'h49,2304,0);
            for(integer unit=0;unit<8;unit=unit+1)begin
                wire_slot=unit<2 ? unit : unit+3;
                media_slot=wire_slot;media_kind=8'h43;media_image_size=32'hf0000100;
                media_generation=32'h100+unit;media_read_only=0;media_mount=1;
                @(negedge core_clk);media_mount=0;repeat(100)@(negedge core_clk);
            end
            if(!rom_loaded || dut.machine_i.hdd.hdd_mounted!==8'hff)$fatal(1,"SmartPort mount/ROM gate");
            core_reset_request=0;
            for(integer unit=0;unit<8;unit=unit+1)begin
                smart_wait_media(unit,0);smart_packet(unit);complete(0);
                smart_wait_media(unit,1);smart_receive(unit);complete(unit==7 ? 32'd4 : 0);
            end
            smart_wait_media(0,0);complete(4);
            wait_stopped();
            for(integer unit=0;unit<8;unit=unit+1)begin
                buffer=24'h02ff80+unit*512;
                for(integer i=0;i<512;i=i+1)if(memory[(buffer+i)>>3][((buffer+i)&7)*8+:8]!==((i*7+19+unit*29)&255))
                    $fatal(1,"SmartPort RAM unit%0d byte%0d",unit,i);
                if(memory[(24'h020100+unit*4)>>3][((24'h020100+unit*4)&7)*8+:8]!==0 ||
                    memory[(24'h020101+unit*4)>>3][((24'h020101+unit*4)&7)*8+:8]!==((19+unit*29)&255) ||
                    memory[(24'h020102+unit*4)>>3][((24'h020102+unit*4)&7)*8+:8]!==((511*7+19+unit*29)&255) ||
                    memory[(24'h020103+unit*4)>>3][((24'h020103+unit*4)&7)*8+:8]!==(unit==7 ? 8'h27 : 8'd0))
                    $fatal(1,"SmartPort CPU stale cache/status unit%0d",unit);
            end
            if(media_request_valid || dut.sd_ack!=0)$fatal(1,"SmartPort completion retained ACK");
            if(memory[24'h020150>>3][15:0]!==16'haa27 || memory[24'h050000>>3]!==64'haaaaaaaaaaaaaaaa)
                $fatal(1,"failed read copied stale sector bytes or lost final error %x",memory[24'h020150>>3]);
            if(memory[24'h020140>>3][7:0]!==hdd_rom[0] || memory[24'h020140>>3][15:8]!==hdd_rom[256] ||
                memory[24'h020140>>3][23:16]!==hdd_rom[2303] || memory[24'h020140>>3][31:24]!==8'h80 ||
                memory[24'h020140>>3][39:32]!==8'h80)
                $fatal(1,"C700/C800 owner,CFFF release,otherCn release %x",memory[24'h020140>>3]);
            $display("PASS: SmartPort production eight-unit stock/max/bypass speed%0d bypass%b32-bitLBA24-bitDMA bankcross cachecoherence/errors",speed,bypass);
        end
    endtask
    task smartport_rom_call_checks(input integer speed);
        integer polls;
        reg [7:0] caller[0:63];integer cursor;
        begin
            @(negedge core_clk);reset=1;core_reset_request=1;repeat(20)@(negedge core_clk);reset=0;
            build_program(0);program_cursor=122880;
            emit(8'h78);immediate(8'h80);sta(16'hc036);immediate(8'h3f);sta(16'hc035);
            immediate(8'h80);sta(16'hc02d);sta(16'hc006);
            emit(8'ha2);emit(8'hff);emit(8'h9a); // Establish the caller's known emulation stack.
            emit(8'h5c);emit(0);emit(8'h20);emit(0);
            // The real IIgs boot scan probes C3 before the slot7 call, leaving
            // internal C800 latched. The C7 firmware must release it itself.
            // Bank0 emulation caller: LDA C300; JSR C70D; extended READ; banked list.
            for(integer i=0;i<64;i=i+1)caller[i]=8'hea;
            caller[0]=8'had;caller[1]=0;caller[2]=8'hc3;
            caller[3]=8'h20;caller[4]=8'h0d;caller[5]=8'hc7;caller[6]=8'h41;
            caller[7]=0;caller[8]=8'h40;caller[9]=2;caller[10]=0;
            cursor=11;
            caller[cursor++]=8'h8f;caller[cursor++]=8'h60;caller[cursor++]=1;caller[cursor++]=2;
            caller[cursor++]=8'h08;caller[cursor++]=8'h68;
            caller[cursor++]=8'h8f;caller[cursor++]=8'h61;caller[cursor++]=1;caller[cursor++]=2;
            caller[cursor++]=8'h8a;caller[cursor++]=8'h8f;caller[cursor++]=8'h62;caller[cursor++]=1;caller[cursor++]=2;
            caller[cursor++]=8'h98;caller[cursor++]=8'h8f;caller[cursor++]=8'h63;caller[cursor++]=1;caller[cursor++]=2;
            caller[cursor++]=8'hba;caller[cursor++]=8'h8a;
            caller[cursor++]=8'h8f;caller[cursor++]=8'h64;caller[cursor++]=1;caller[cursor++]=2;
            caller[cursor++]=8'hdb;
            for(integer i=0;i<64;i=i+1)memory[(24'h002000+i)>>3][((24'h002000+i)&7)*8+:8]=caller[i];
            memory[24'h024000>>3]=64'h00000003ff000803;memory[24'h024008>>3]=0;
            memory[24'h020160>>3]=0;
            packet(8'h43,16,speed);packet(8'h41,131072,0);packet(8'h42,4096,0);packet(8'h46,4096,0);packet(8'h49,2304,0);
            media_slot=10;media_kind=8'h43;media_image_size=32'hf0000100;media_generation=32'h107;media_read_only=0;
            media_mount=1;@(negedge core_clk);media_mount=0;repeat(100)@(negedge core_clk);core_reset_request=0;
            polls=0;while(!media_request_valid && polls<3000000)begin @(negedge core_clk);polls=polls+1;end
            if(!media_request_valid || media_request_write || media_request_lba!=0 || media_request_block_count!=16'h0a02 ||
                media_request_generation!=32'h107)$fatal(1,"actual SmartPort ROM call did not issue unit8 block0 PC%x",dut.machine_i.cpu_addr);
            smart_packet(7);complete(0);wait_stopped();
            for(integer i=0;i<512;i=i+1)if(memory[(24'h03ff00+i)>>3][((24'h03ff00+i)&7)*8+:8]!==((i*7+19+7*29)&255))
                $fatal(1,"actual SmartPort ROM banked buffer byte%0d",i);
            if(memory[24'h020160>>3][7:0]!==0 || (memory[24'h020160>>3][15:8]&8'h05)!==8'h04 ||
                memory[24'h020160>>3][31:16]!==16'h0200 || memory[24'h020160>>3][39:32]!==8'hff)
                $fatal(1,"actual SmartPort ROM A/P/X/Y/SP return=%x",memory[24'h020160>>3]);
            $display("PASS: prelatched INTC8 actual C70D/C800 extended SmartPort ROM unit8 banked param+DMA return/status/registers speed%0d",speed);
        end
    endtask
    task smartport_reset_checks;
        reg [31:0] abandoned_id,abandoned_gen,new_id;
        begin
            disk_bypass=0;start_machine(1,6);wait_media(0);
            abandoned_id=media_request_id;abandoned_gen=media_request_generation;
            core_reset_request=1;repeat(100)@(negedge core_clk);
            if(media_request_valid || dut.pending || dut.sd_ack!=0 || dut.machine_i.hdd.DMA)
                $fatal(1,"reset retained interrupted HDD DMA/request");
            core_reset_request=0;wait_media(0);new_id=media_request_id;
            if(new_id==abandoned_id)$fatal(1,"reset reused request identity");
            @(negedge core_clk);media_complete_request_id=abandoned_id;media_complete_generation=abandoned_gen;
            media_complete_status=0;media_complete_block_count=2;media_complete=1;
            @(negedge core_clk);media_complete=0;repeat(30)@(negedge core_clk);
            if(!media_request_valid || media_request_id!=new_id || !dut.sd_ack[0])
                $fatal(1,"stale interrupted completion consumed new operation");
            packet(8'h48,512,0);complete(0);wait_media(1);receive_sector();complete(0);wait_stopped();
            $display("PASS: SmartPort interrupted HDD reset, stale completion rejection and successful fresh retry");
        end
    endtask
    reg smart_eject_watch=0;
    always @(negedge core_clk)if(smart_eject_watch && !media_state_eject_pending && dut.machine_i.hdd.hdd_mounted[1:0]!=0)
        $fatal(1,"host eject barrier released before both native drives unmounted");
    task smartport_metadata_checks;
        integer polls;
        reg [63:0] held_size;
        begin
            // The preceding CPU is stopped; publish mount0, then queue other
            // units and a replacement of the same unit while its tuple lives.
            media_slot=0;media_kind=8'h43;media_image_size=10;media_read_only=0;media_generation=32'h900;
            media_mount=1;@(negedge core_clk);media_mount=0;
            polls=0;while(dut.hdd_mount_pulse!=1 && polls<100)begin @(negedge core_clk);polls=polls+1;end
            if(dut.hdd_mount_pulse!=1)$fatal(1,"first metadata publication never started");
            held_size=dut.mount_size;
            media_slot=1;media_image_size=20;media_read_only=1;media_mount=1;
            @(negedge core_clk);media_mount=0;
            media_slot=5;media_image_size=30;media_read_only=0;media_mount=1;
            @(negedge core_clk);media_mount=0;
            media_slot=0;media_kind=8'h40;media_image_size=2048;media_read_only=1;media_mount=1;
            @(negedge core_clk);media_mount=0;
            if(dut.mount_size!=held_size || dut.hdd_mount_pulse!=1 || dut.mount_readonly)
                $fatal(1,"rapid mount overwrote a held native tuple");
            polls=0;while((dut.hdd_metadata_pending!=0 || dut.mount_hold!=0) && polls<1000)begin @(negedge core_clk);polls=polls+1;end
            repeat(10)@(negedge core_clk);
            if(load_error || dut.hdd_metadata_pending!=0 || dut.machine_i.hdd.hdd_mounted!==8'h87 ||
                dut.machine_i.hdd.hdd_size[0]!=4 || dut.machine_i.hdd.hdd_size[1]!=20 ||
                dut.machine_i.hdd.hdd_size[2]!=30 || (dut.machine_i.hdd.hdd_protect&7)!=3)
                $fatal(1,"queued/latest mount metadata lost mounted=%x size0=%d size1=%d size2=%d protect=%x",
                    dut.machine_i.hdd.hdd_mounted,dut.machine_i.hdd.hdd_size[0],dut.machine_i.hdd.hdd_size[1],
                    dut.machine_i.hdd.hdd_size[2],dut.machine_i.hdd.hdd_protect);
            // A whole-adapter reset clears native metadata, unlike a warm CPU
            // reset. Then two ejects queue during a real disk DMA operation.
            disk_bypass=0;start_machine(1,6);
            media_slot=1;media_kind=8'h43;media_image_size=10;media_read_only=0;media_generation=32'h901;media_mount=1;
            @(negedge core_clk);media_mount=0;wait_media(0);
            if(dut.machine_i.hdd.hdd_mounted!==3)$fatal(1,"adapter reset retained stale native drives");
            media_slot=0;media_eject=1;@(negedge core_clk);media_eject=0;
            media_slot=1;media_eject=1;@(negedge core_clk);media_eject=0;smart_eject_watch=1;
            repeat(10)@(negedge core_clk);
            if(!dut.hdd_dma || dut.eject_pending[1:0]!=3 || !media_state_eject_pending)
                $fatal(1,"ejects did not wait for active DMA");
            packet(8'h48,512,0);complete(0);wait_stopped();
            polls=0;while(media_state_eject_pending && polls<1000)begin @(negedge core_clk);polls=polls+1;end
            smart_eject_watch=0;
            if(media_state_eject_pending || dut.mounted[1:0]!=0 || dut.machine_i.hdd.hdd_mounted!==0 ||
                dut.machine_i.hdd.reg_status!=8'h2f || media_request_valid)
                $fatal(1,"combined eject lost native unmount mounted=%x pending=%x native=%x",
                    dut.mounted,dut.eject_pending,dut.machine_i.hdd.hdd_mounted);
            $display("PASS: rapid different/same-unit mounts retain latest metadata; combined DMA-blocked ejects publish all native unmounts before host release");
        end
    endtask

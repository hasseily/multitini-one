// SPDX-License-Identifier: GPL-3.0-only
// Included inside tb_apple_iigs_adapter. Exercise the imported drive's actual
// BRAM consumption and RMW; never force BRAM_ADDR, BRAM_DATA or write outputs.
`define NWOZ dut.machine_i.iwmc.drive35
    reg [16:0] native_woz_position;

    task native_woz_setup;
        begin
            force `NWOZ.motor_spinning=1'b1;
            force `NWOZ.drive_ready=1'b1;
            force `NWOZ.prev_drive_ready=1'b1;
            force `NWOZ.prev_motor_for_position=1'b1;
            force `NWOZ.TRACK_LOADED=1'b1;
            force `NWOZ.TRACK_LOAD_COMPLETE=1'b0;
            force `NWOZ.bram_first_read_pending=1'b0;
            force `NWOZ.flux_startup_delay=6'd0;
            force `NWOZ.weak_bit_active=1'b0;
            force `NWOZ.IS_FLUX_TRACK=1'b0;
            force dut.machine_i.iwmc.stable_side_reg=1'b0;
            force `NWOZ.WRITE_STROBE=1'b0;
            force `NWOZ.bit_timer=6'd10;
            force `NWOZ.bit_half_timer=6'd10;
            force `NWOZ.bit_position=native_woz_position;
        end
    endtask
    task native_woz_release;
        begin
            release `NWOZ.motor_spinning;
            release `NWOZ.drive_ready;
            release `NWOZ.prev_drive_ready;
            release `NWOZ.prev_motor_for_position;
            release `NWOZ.TRACK_LOADED;
            release `NWOZ.TRACK_LOAD_COMPLETE;
            release `NWOZ.bram_first_read_pending;
            release `NWOZ.flux_startup_delay;
            release `NWOZ.weak_bit_active;
            release `NWOZ.IS_FLUX_TRACK;
            release dut.machine_i.iwmc.stable_side_reg;
            release `NWOZ.WRITE_STROBE;
            release `NWOZ.WRITE_MODE;
            release `NWOZ.WRITE_BIT;
            release `NWOZ.bit_timer;
            release `NWOZ.bit_half_timer;
            release `NWOZ.bit_position;
        end
    endtask
    task native_woz_settle;
        begin
            repeat(4)@(posedge dut.clk14);
            @(negedge core_clk);
        end
    endtask
    task native_woz_read_checks;
        reg [7:0] expected;
        begin
            native_woz_position=0;native_woz_setup();
            force `NWOZ.WRITE_MODE=1'b0;
            // Byte-address transitions include the wrap-end byte. Compare both
            // the input byte and actual native bit/flux consumer after settling.
            for(integer p=0;p<18;p=p+1)begin
                @(negedge core_clk);native_woz_position=p<17 ? p:4095;
                native_woz_settle();
                expected=woz_image[1536+(native_woz_position>>3)];
                if(`NWOZ.BRAM_DATA!==expected ||
                    `NWOZ.current_bit!==expected[7-native_woz_position[2:0]] ||
                    `NWOZ.FLUX_TRANSITION!==expected[7-native_woz_position[2:0]])
                    $fatal(1,"native WOZ bitstream consumption pos=%0d byte=%x expected=%x bit=%b flux=%b",
                        native_woz_position,`NWOZ.BRAM_DATA,expected,`NWOZ.current_bit,`NWOZ.FLUX_TRANSITION);
            end
            // Actual timer-driven 7->8 advance. Prefetch begins before the
            // native edge; check the byte consumed at the next mid-cell pulse.
            force `NWOZ.bit_cell_base=6'd8;
            force `NWOZ.bit_half_base=6'd4;
            force `NWOZ.bit_cell_step=10'd0;
            force `NWOZ.bit_half_step=10'd0;
            for(integer boundary=0;boundary<2;boundary=boundary+1)begin
                native_woz_position=boundary==0 ? 7:127;
                force `NWOZ.bit_position=native_woz_position;
                force `NWOZ.bit_timer=6'd10;
                native_woz_settle();
                @(negedge dut.clk14);
                force `NWOZ.bit_timer=6'd2;
                force `NWOZ.bit_half_timer=6'd4;
                release `NWOZ.bit_timer;release `NWOZ.bit_position;
                repeat(7)@(posedge dut.clk14);
                @(negedge core_clk);
                expected=woz_image[1536+((native_woz_position+1)>>3)];
                if(`NWOZ.bit_position!==native_woz_position+1 || `NWOZ.BRAM_DATA!==expected ||
                    `NWOZ.FLUX_TRANSITION!==expected[7])
                    $fatal(1,"native WOZ lookahead missed byte boundary pos=%0d timer=%0d data=%x expected=%x flux=%b",
                        `NWOZ.bit_position,`NWOZ.bit_timer,`NWOZ.BRAM_DATA,expected,`NWOZ.FLUX_TRANSITION);
            end
            release `NWOZ.bit_cell_base;release `NWOZ.bit_half_base;
            release `NWOZ.bit_cell_step;release `NWOZ.bit_half_step;

            // Feed the cached bytes through the actual flux initial-load and
            // prefetch states, using bounded timer state instead of media assets.
            force `NWOZ.IS_FLUX_TRACK=1'b1;
            force `NWOZ.FLUX_DATA_SIZE=32'd512;
            force `NWOZ.flux_byte_addr=16'd0;
            force `NWOZ.flux_waiting_bram=1'b0;
            force `NWOZ.flux_byte_pending=1'b0;
            force `NWOZ.next_byte_valid=1'b0;
            force `NWOZ.flux_byte_counter=8'd20;
            native_woz_settle();
            @(negedge dut.clk14);
            release `NWOZ.flux_byte_addr;release `NWOZ.flux_waiting_bram;
            release `NWOZ.next_byte_valid;release `NWOZ.flux_byte_counter;
            force `NWOZ.flux_byte_pending=1'b1;release `NWOZ.flux_byte_pending;
            repeat(2)@(posedge dut.clk14);
            @(negedge core_clk);
            if(`NWOZ.flux_byte_counter!==woz_image[1536] || `NWOZ.flux_byte_addr!==16'd1)
                $fatal(1,"native WOZ initial flux byte wrong counter=%x addr=%0d",
                    `NWOZ.flux_byte_counter,`NWOZ.flux_byte_addr);
            @(negedge dut.clk14);
            force `NWOZ.flux_byte_counter=8'd3;release `NWOZ.flux_byte_counter;
            force `NWOZ.flux_tick=1'b1;
            repeat(2)@(posedge dut.clk14);
            @(negedge core_clk);
            if(!`NWOZ.next_byte_valid || `NWOZ.next_flux_byte!==woz_image[1537])
                $fatal(1,"native WOZ flux prefetch wrong valid=%b byte=%x expected=%x",
                    `NWOZ.next_byte_valid,`NWOZ.next_flux_byte,woz_image[1537]);
            release `NWOZ.flux_tick;release `NWOZ.FLUX_DATA_SIZE;
            native_woz_release();
            $display("PASS: native WOZ bitstream bytes, timer lookahead and flux initial/prefetch consumption");
        end
    endtask
    task native_woz_write_bit(input integer position,input bit value);
        reg [7:0] expected;
        begin
            native_woz_position=position;
            force `NWOZ.bit_position=native_woz_position;
            force `NWOZ.WRITE_MODE=1'b1;
            force `NWOZ.WRITE_STROBE=1'b0;
            native_woz_settle();
            expected=woz_image[1536+(position>>3)];
            expected=value ? (expected | (8'b1<<(7-(position&7)))) :
                (expected & ~(8'b1<<(7-(position&7))));
            @(negedge dut.clk14);release `NWOZ.bit_position;
            if(value)force `NWOZ.WRITE_BIT=1'b1;
            else force `NWOZ.WRITE_BIT=1'b0;
            force `NWOZ.WRITE_STROBE=1'b1;
            @(posedge dut.clk14);@(negedge core_clk);
            if(`NWOZ.bit_position!==position+1)
                $fatal(1,"native WOZ WRITE_STROBE did not advance position %0d",position);
            force `NWOZ.WRITE_STROBE=1'b0;
            @(posedge dut.clk14);@(negedge core_clk);
            if(!`NWOZ.WRITE_WE_OUT || `NWOZ.WRITE_ADDR_OUT!==(position>>3) || `NWOZ.WRITE_BYTE_OUT!==expected)
                $fatal(1,"native WOZ RMW wrong pos=%0d addr=%0d data=%x expected=%x",
                    position,`NWOZ.WRITE_ADDR_OUT,`NWOZ.WRITE_BYTE_OUT,expected);
            repeat(8)@(negedge core_clk);
            if(dut.g_woz[0].floppy_i.track_ram_side0.mem[position>>3]!==expected)
                $fatal(1,"native WOZ RMW did not reach production track RAM");
            woz_image[1536+(position>>3)]=expected;
        end
    endtask
    task native_woz_write_checks;
        reg [7:0] expected;
        begin
            native_woz_position=7;native_woz_setup();
            native_woz_write_bit(7,0);
            if(dut.g_woz[0].floppy_i.track_ram_side0.mem[1]!==woz_image[1537])
                $fatal(1,"native WOZ 7->8 RMW damaged adjacent byte");
            native_woz_write_bit(8,1);

            // A retained native write pulse can coincide with an immediate
            // mode-register access while the IWM is inactive. PH2 is qualified
            // by the real cache stall gate: assert it after phase1 capture.
            native_woz_position=7;
            force `NWOZ.bit_position=native_woz_position;
            force `NWOZ.WRITE_STROBE=1'b0;
            force `NWOZ.bit_timer=6'd2;
            release `NWOZ.WRITE_MODE;
            force dut.machine_i.iwmc.iwm.m_rw_mode=1'b1;
            force dut.machine_i.iwmc.iwm_active=1'b0;
            force dut.machine_i.iwmc.mode_reg=8'h08;
            force dut.machine_i.iwmc.q6=1'b1;
            force dut.machine_i.iwmc.q7=1'b1;
            force dut.machine_i.iwmc.DEVICE_SELECT=1'b1;
            force dut.machine_i.iwmc.WR_CYCLE=1'b0;
            force dut.machine_i.iwmc.A=16'h00ef;
            force dut.machine_i.iwmc.D_IN=8'h02;
            force dut.machine_i.ph2_en=1'b1;
            force dut.cache_stall=1'b0;
            native_woz_settle();
            while(dut.phase!=2)@(negedge core_clk);
            expected=woz_image[1536]|8'h01;
            force dut.cache_stall=1'b1;
            force `NWOZ.WRITE_BIT=1'b1;
            force `NWOZ.WRITE_STROBE=1'b1;
            release `NWOZ.bit_position;
            @(posedge dut.clk14);@(negedge core_clk);
            if(!`NWOZ.WRITE_MODE || `NWOZ.write_addr_d1!==16'd0)
                $fatal(1,"late native IWM mode fixture did not select the current write byte");
            force `NWOZ.WRITE_STROBE=1'b0;
            @(posedge dut.clk14);@(negedge core_clk);
            if(!`NWOZ.WRITE_WE_OUT || `NWOZ.WRITE_ADDR_OUT!==16'd0 || `NWOZ.WRITE_BYTE_OUT!==expected)
                $fatal(1,"late cache/mode transition broke native WOZ RMW addr=%0d byte=%x expected=%x",
                    `NWOZ.WRITE_ADDR_OUT,`NWOZ.WRITE_BYTE_OUT,expected);
            repeat(8)@(negedge core_clk);
            if(dut.g_woz[0].floppy_i.track_ram_side0.mem[0]!==expected)
                $fatal(1,"late-mode native RMW did not reach production track RAM");
            woz_image[1536]=expected;
            release dut.machine_i.iwmc.iwm.m_rw_mode;
            release dut.machine_i.iwmc.iwm_active;
            release dut.machine_i.iwmc.mode_reg;
            release dut.machine_i.iwmc.q6;release dut.machine_i.iwmc.q7;
            release dut.machine_i.iwmc.DEVICE_SELECT;release dut.machine_i.iwmc.WR_CYCLE;
            release dut.machine_i.iwmc.A;release dut.machine_i.iwmc.D_IN;
            release dut.machine_i.ph2_en;release dut.cache_stall;
            native_woz_release();
            $display("PASS: native WOZ byte-boundary RMW, production track writes and late cache/mode transition");
        end
    endtask
`undef NWOZ

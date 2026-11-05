// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps

// Original synthetic flux and CPU transactions. No system ROM or disk image.
module tb_apple_iigs_iwm;
    reg clk=0,reset=1;always #1 clk=~clk;
    reg [3:0] phase=0;
    always @(negedge clk)phase<=phase==13 ? 0:phase+1;
    integer scenario=0,poll_div=1,bus_cycles=0;
    iwm_motor_bus_probe motor_bus(.clk(clk), .run(scenario == 6));
    always @(negedge clk)if(reset)bus_cycles<=0;else if(phase==13)bus_cycles<=bus_cycles+1;
    wire ce7=phase[0];
    reg manual_cen=0,manual_rd=0;reg [3:0] addr=4'hc;reg [7:0] mode=8'h0f;
    reg manual_wr=0,manual_q6=0,manual_q7=0;
    reg [7:0] write_data=8'hff;
    wire cen=scenario==0 ? phase==7:manual_cen;
    wire rd=scenario==0 ? (cen&&(bus_cycles%poll_div==0)):manual_rd;
    reg [7:0] track[0:65535];reg [7:0] bram_data=0;
    wire [15:0] bram_addr;wire drive_flux,spinning,ready;
    wire tach_n, drive_sense;
    reg [3:0] sony_phases = 4'd0;
    reg [2:0] mechanics_sense = 3'b110;
    reg mechanics_sel = 1'b1;
    reg [1:0] drive_clock_phase = 2'd0;
    reg sparse_drive_clock = 1'b0;
    reg track_loaded = 1'b1;
    always @(negedge clk) drive_clock_phase <= drive_clock_phase + 2'd1;
    wire drive_ce = !sparse_drive_clock || drive_clock_phase == 2'd3;
    integer native_ticks = 0;
    always @(posedge clk) if (!reset && drive_ce) native_ticks++;
    wire [16:0] position;wire [5:0] timer;
    wire [8:0] head_qtrack;
    integer bit_count=0,expected_count=0;
    reg [7:0] expected[0:4095];
    always @(posedge clk)bram_data<=track[bram_addr];
    flux_drive drive(
      .IS_35_INCH(1'b1),.DRIVE_ID(2'd2),.CLK_14M(clk),.RESET(reset),.CLOCK_ENABLE(drive_ce),
      .PHASES(sony_phases),.IMMEDIATE_PHASES(sony_phases),.IWM_MODE(mode[4:0]),
      .LATCHED_SENSE_REG(scenario == 4 ? 3'b011 : scenario == 5 ? mechanics_sense : 3'd0),
      .MOTOR_ON(1'b1),.SW_MOTOR_ON(1'b1),.DISKREG_SEL(scenario == 4 || (scenario == 5 && mechanics_sel)),
      .SEL35(1'b1),.DRIVE_SELECT(1'b0),.DRIVE_SLOT(1'b0),
      .DISK_MOUNTED(1'b1),.DISK_WP(1'b0),.DOUBLE_SIDED(1'b1),.FLUX_TRANSITION(drive_flux),
      .MOTOR_SPINNING(spinning),.DRIVE_READY(ready),.BIT_POSITION(position),.BIT_TIMER_OUT(timer),
      .TACH_N_OUT(tach_n),.SENSE(drive_sense),.HEAD_QTRACK(head_qtrack),
      .TRACK_BIT_COUNT(32'(bit_count)),.TRACK_LOADED(track_loaded),.TRACK_LOAD_COMPLETE(1'b0),
      .BRAM_ADDR(bram_addr),.BRAM_DATA(bram_data),.IS_FLUX_TRACK(1'b0),.FLUX_DATA_SIZE(32'd0),.FLUX_TOTAL_TICKS(32'd0),
      .SD_TRACK_ACK(1'b0),.WRITE_BIT(1'b0),.WRITE_STROBE(1'b0),.WRITE_MODE(1'b0),
      .CHUNK_LOADED(2'd0),.CHUNK_LOADING(1'b0)
    );
    reg gap_flux=0,block_flux=0;integer bitclock=0;
    always @(negedge clk)begin
      gap_flux<=0;
      if(!reset&&!block_flux)begin
        if(bitclock==27)begin gap_flux<=1;bitclock<=0;end else bitclock<=bitclock+1;
      end
    end
    wire flux=scenario==0 ? drive_flux:gap_flux;
    wire [7:0] dataout;wire bytevalid,write_strobe;
    integer written_bits=0;
    reg previous_write_strobe=0;
    always @(posedge clk) begin
      previous_write_strobe<=write_strobe;
      if(write_strobe && !previous_write_strobe)written_bits<=written_bits+1;
    end
    iwm_flux dut(
      .CLK_14M(clk),.CLK_7M_EN(ce7),.RESET(reset),.CEN(cen),.ADDR(addr),.RD(rd),.WR(manual_wr),.DATA_IN(write_data),.DATA_OUT(dataout),
      .SW_PHASES(4'd0),.SW_MOTOR_ON(1'b1),.SW_DRIVE_SEL(1'b0),.SW_Q6(manual_q6),.SW_Q7(manual_q7),.SW_Q7_MODE(manual_q7),.SW_MODE(mode),
      .FLUX_TRANSITION(flux),.MOTOR_ACTIVE(1'b1),.MOTOR_SPINNING(scenario==0 ? spinning:1'b1),
      .DISK_READY(scenario==0 ? ready:1'b1),.DISK_MOUNTED(1'b1),.IS_35_INCH(1'b1),
      .SENSE_BIT(1'b0),.LATCHED_SENSE_REG(3'd0),.DISKREG_SEL(1'b0),.DISK_BIT_POSITION(scenario==0 ? position:17'd0),
      .FLUX_BIT_TIMER(scenario==0 ? timer:6'd0),.SP_RD_DATA_VALID(1'b0),.SP_RD_DATA(8'd0),.SP_BSY(1'b0),.CBUS_ENABLE2(1'b0)
      ,.FLUX_WRITE_STROBE(write_strobe),.DEBUG_BYTE_VALID(bytevalid),.DEBUG_RSH(),.DEBUG_STATE()
    );
    task append_bit(input bit value);
      begin track[bit_count/8][7-bit_count%8]=value;bit_count=bit_count+1;end
    endtask
    task sony_command(input [2:0] phases);
      begin
        @(negedge clk);
        sony_phases = {1'b0, phases};
        repeat (4) @(negedge clk);
        sony_phases = {1'b1, phases};
        repeat (4) @(negedge clk);
        sony_phases = {1'b0, phases};
        repeat (4) @(negedge clk);
      end
    endtask
    task tach_half_period(input integer expected_ticks);
      integer elapsed;
      bit previous_tach;
      begin
        elapsed = 0;
        previous_tach = tach_n;
        do begin
          @(posedge clk);
          if (drive_ce) elapsed++;
          @(negedge clk);
        end while (tach_n == previous_tach);
        if (elapsed != expected_ticks || drive_sense !== tach_n)
          $fatal(1, "Sony tach half-period expected=%0d actual=%0d sense=%b tach=%b",
                 expected_ticks, elapsed, drive_sense, tach_n);
      end
    endtask
    task write_sony_byte;
      begin
        @(negedge clk);addr=4'hd;manual_cen=1;manual_wr=1;manual_q6=1;
        @(negedge clk);addr=4'hc;manual_cen=0;manual_wr=0;manual_q6=0;
      end
    endtask
    task append_byte(input [7:0] value,input bit sync);
      begin
        expected[expected_count]=value;expected_count=expected_count+1;
        for(integer b=7;b>=0;b=b-1)append_bit(value[b]);
        if(sync)begin append_bit(0);append_bit(0);end
      end
    endtask
    integer cycle=0,completed=0,reads=0,acks=0,revolutions=0,previous_pos=0;
    integer cell_cycles=0,measured_cells=0,cell_sum=0,shift_count=0;
    reg armed=0;
    always @(posedge clk)if(!reset)begin
      cycle=cycle+1;
      if(armed&&(dut.shift_edge0_now||dut.shift_edge1_now))shift_count=shift_count+1;
      if(scenario==0)begin
        if(bytevalid)begin
          if(dut.byte_complete_data!==expected[completed%expected_count])
            $fatal(1,"IWM completed-byte mismatch index=%0d",completed);
          completed=completed+1;
        end
        if(cen&&rd&&dataout[7])begin
          if(dataout!==expected[reads%expected_count])$fatal(1,"CPU data byte mismatch index=%0d",reads);
          reads=reads+1;
        end
        if(dut.prev_cen&&!cen&&dut.rd_in_progress&&dut.rd_latched_valid)begin
          if(dut.rd_data_latched!==expected[acks%expected_count]||!dut.rd_ack_take||
             !(dut.rd_from_completion||!dut.byte_completing||dut.rd_ack_promote_queued))
            $fatal(1,"CPU ACK mismatch index=%0d",acks);
          acks=acks+1;
        end
        if(ready)begin
          cell_cycles=cell_cycles+1;
          if(position!=previous_pos)begin
            // Skip initial partial cell, then exactly 1000 successive cells.
            if(measured_cells>=1&&measured_cells<=1000)begin
              if(cell_cycles!=28&&cell_cycles!=29)$fatal(1,"Invalid Sony bit-cell width %0d",cell_cycles);
              cell_sum=cell_sum+cell_cycles;
            end
            measured_cells=measured_cells+1;cell_cycles=0;
          end
        end
        if(position<previous_pos)revolutions=revolutions+1;
        previous_pos=position;
        // The final byte may still await its next CPU bus pulse at track wrap.
        // Drain that ACK before the next revolution's first byte can complete.
        if(revolutions>=2&&acks==expected_count*2)begin
          if(completed!=expected_count*2||reads!=completed||acks!=reads)
            $fatal(1,"Incomplete stream complete=%0d expected=%0d read=%0d ack=%0d",completed,expected_count*2,reads,acks);
          if(measured_cells<1001||cell_sum!=28636)$fatal(1,"Wrong 1000-cell mean %0d",cell_sum);
          $display("PASS: IWM synthetic completed/CPU/ACK stream poll=%0d bytes=%0d mean1000=%0d",poll_div*14,completed,cell_sum);
          $finish;
        end
      end
      if(cycle>5000000)$fatal(1,"IWM regression timeout");
    end
    task access(input [3:0] a);
      begin
        @(negedge clk);addr=a;manual_cen=1;manual_rd=1;
        @(negedge clk);manual_cen=0;manual_rd=0;
        repeat(2)@(negedge clk);
      end
    endtask
    initial begin : scenarios
      reg [7:0] first_byte;integer before_status, command_ticks;
      reg [16:0] position_at_lock;
      integer tach_half_ticks[5] = '{18170, 16688, 15168, 13636, 12134};
      if(!$value$plusargs("SCENARIO=%d",scenario))scenario=0;
      if(!$value$plusargs("POLL_DIV=%d",poll_div))poll_div=1;
      if (scenario == 6) disable scenarios;
      for(integer i=0;i<65536;i=i+1)track[i]=0;
      // Long sync gaps plus all 128 high-bit byte values, with known prologues.
      for(integer group=0;group<8;group=group+1)begin
        for(integer i=0;i<8;i=i+1)append_byte(8'hff,1);
        append_byte(8'hd5,0);append_byte(8'haa,0);append_byte(8'h96,0);
        for(integer i=0;i<128;i=i+1)append_byte(8'h80|8'(i),0);
        append_byte(8'hd5,0);append_byte(8'haa,0);append_byte(8'had,0);
      end
      repeat(32)@(negedge clk);reset=0;
      if (scenario == 5) begin
        track_loaded = 0;
        command_ticks = native_ticks;
        sony_command(3'b010); // $08 motor on
        if (!spinning || ready || drive_sense !== 1'b1)
          $fatal(1,"Cold Sony motor started ready");
        wait(ready);@(negedge clk);
        if (native_ticks-command_ticks < 2800000 || native_ticks-command_ticks > 2800016 || drive_sense !== 1'b0)
          $fatal(1,"Sony cold lock at %0d ticks, sense=%b",native_ticks-command_ticks,drive_sense);
        sony_command(3'b110); // $09 motor off
        if (!spinning || !ready) $fatal(1,"Sony motor stopped supplying data during coast");
        command_ticks = native_ticks;
        sony_command(3'b010); // $08 motor on during coast
        if (!spinning || ready || drive_sense !== 1'b1)
          $fatal(1,"Warm Sony motor started ready");
        repeat (600000) @(negedge clk);
        track_loaded = 1;
        wait(ready);@(negedge clk);
        if (native_ticks-command_ticks < 1204000 || native_ticks-command_ticks > 1204016 || drive_sense !== 1'b0)
          $fatal(1,"Sony warm lock at %0d ticks, sense=%b",native_ticks-command_ticks,drive_sense);
        position_at_lock = position;
        @(negedge clk);
        if (position != position_at_lock && position != position_at_lock + 17'd1 &&
            !(position == 17'd0 && position_at_lock == 17'(bit_count-1)))
          $fatal(1,"Sony warm lock reset spindle position from %0d to %0d",position_at_lock,position);
        track_loaded = 0;

        sparse_drive_clock = 1;
        mechanics_sense = 3'b010; // $0A track zero
        sony_command(3'b001); // $04 step inward from the default track 0
        if (head_qtrack != 9'd4 || drive_sense !== 1'b1)
          $fatal(1,"Sony head did not leave track zero");
        sony_command(3'b100); // $01 direction outward
        mechanics_sel = 0;
        mechanics_sense = 3'b001; // $04 step acknowledgement
        command_ticks = native_ticks;
        sony_command(3'b001); // $04 return to track 0
        if (drive_sense !== 1'b0) $fatal(1,"Sony STEP did not acknowledge the command");
        wait(drive_sense);@(negedge clk);
        if (native_ticks-command_ticks < 400 || native_ticks-command_ticks > 405)
          $fatal(1,"Sony STEP acknowledged at %0d ticks",native_ticks-command_ticks);
        mechanics_sel = 1;
        mechanics_sense = 3'b010; // $0A track zero
        @(negedge clk);
        if (head_qtrack != 9'd0 || drive_sense !== 1'b1)
          $fatal(1,"Sony home sensor asserted before mechanical settling");
        wait(!drive_sense);@(negedge clk);
        if (native_ticks-command_ticks < 85909 || native_ticks-command_ticks > 85914)
          $fatal(1,"Sony home sensor settled at %0d ticks",native_ticks-command_ticks);
        sony_command(3'b001); // end-stop step must not restart the home sensor delay
        if (head_qtrack != 9'd0 || drive_sense !== 1'b0)
          $fatal(1,"Sony head moved through the home stop");
        $display("PASS: Sony cold/warm lock, spindle continuity, STEP acknowledgement, and track-zero settling in native ticks");
        $finish;
      end
      if (scenario == 4) begin
        track_loaded = 1'b0;
        if (tach_n !== 1'b1 || drive_sense !== 1'b1)
          $fatal(1, "Stopped Sony tach is not high");
        sony_command(3'b010); // $08 motor on
        for (integer zone = 0; zone < 5; zone++) begin
          if (zone != 0) repeat (16) sony_command(3'b001); // $04 step inward
          repeat (2) @(negedge tach_n);
          @(negedge clk);
          tach_half_period(tach_half_ticks[zone]);
          tach_half_period(tach_half_ticks[zone]);
        end
        // Native mode enables one mechanics tick every four primary edges.
        sparse_drive_clock = 1'b1;
        @(negedge tach_n);
        @(negedge clk);
        tach_half_period(tach_half_ticks[4]);
        tach_half_period(tach_half_ticks[4]);
        sparse_drive_clock = 1'b0;
        sony_command(3'b110); // $09 motor off
        wait (!spinning);
        @(negedge clk);
        if (tach_n !== 1'b1 || drive_sense !== 1'b1)
          $fatal(1, "Sony tach continued after the spindle stopped");
        $display("PASS: Sony tach follows all five spindle zones and the mechanics clock enable without a cached track");
        $finish;
      end
      if (scenario != 4) force drive.sony_motor_on = 1'b1;
      if(scenario==3)begin
        manual_q7=1;
        repeat(8)@(negedge clk);
        if(dataout!==8'hff)$fatal(1,"Sony write was not initially ready: %02x",dataout);
        write_sony_byte();
        repeat(40)@(negedge clk);
        write_sony_byte();
        repeat(800)@(negedge clk);
        if(written_bits!=16 || dataout!==8'hbf)
          $fatal(1,"Sony underrun before the third byte: bits=%0d handshake=%02x",written_bits,dataout);
        write_sony_byte();
        repeat(400)@(negedge clk);
        if(written_bits!=16 || dataout!==8'h3f)
          $fatal(1,"Sony underrun restarted or advertised a full latch as ready: bits=%0d handshake=%02x",written_bits,dataout);
        manual_q7=0;repeat(8)@(negedge clk);
        manual_q7=1;repeat(8)@(negedge clk);
        if(dataout!==8'hff)$fatal(1,"Sony handshake did not recover when Q7 reentered write mode: %02x",dataout);
        repeat(40)@(negedge clk);
        if(written_bits!=16)$fatal(1,"Sony reentry shifted the stale underrun byte");
        write_sony_byte();repeat(400)@(negedge clk);
        if(written_bits!=24)$fatal(1,"Sony shifter did not accept a fresh byte after Q7 reentry");
        $display("PASS: Sony async underrun latches busy until Q7 reset and rejects stale write data");
        $finish;
      end else if(scenario!=0)begin
        wait(bytevalid);@(posedge clk);@(negedge clk);block_flux=1;armed=1;
        first_byte=dut.m_data;
        if(!first_byte[7]||dut.latch_hold_cnt!=8||dut.m_data_read)$fatal(1,"Natural byte did not initialize latch hold");
        if(scenario==1)begin
          wait(shift_count>=12);@(negedge clk);
          before_status=dut.m_data;access(4'hd);repeat(80)@(negedge clk);
          if(before_status!=dut.m_data||dut.async_update_pending)$fatal(1,"Status access consumed old byte");
        end else begin
          wait(shift_count>=1);@(negedge clk);mode=8'h0e;repeat(2)@(negedge clk);
        end
        if(dut.latch_hold_cnt!=0)$fatal(1,"Latch hold did not expire or clear when disabled");
        access(4'h0);
        if(!dut.async_update_pending||dut.m_data_read)$fatal(1,"Sony phase access failed to schedule unconsumed byte");
        repeat(80)@(negedge clk);addr=4'hc;@(negedge clk);
        if(dut.async_update_pending||dataout!=0||dut.m_data_read)$fatal(1,"Old unread byte survived async deadline");
        $display("PASS: IWM no-ACK latch gap scenario=%0d hold=%0d visible=%02x",scenario,dut.latch_hold_cnt,dataout);
        $finish;
      end
    end
endmodule

// Use actual IWM soft-switch transactions. Mechanics can run without track data.
module iwm_motor_bus_probe(input clk, input run);
    localparam integer SHORT_DELAY = 8126;
    localparam integer LONG_DELAY = 16645630;
    reg reset=1, selected=0, reading=1;
    reg [3:0] offset=0, mounted=0;
    reg [7:0] data_in=0, diskreg=8'h40;
    wire [7:0] data_out;

    iwm_woz iwm(
      .CLK_14M(clk),.CLK_7M_EN(1'b1),.Q3(1'b0),.PH0(1'b0),.PH2(selected),.RESET(reset),
      .IO_SELECT(selected),.DEVICE_SELECT(selected),.WR_CYCLE(reading),.VDA(1'b1),
      .A({12'hc0e,offset}),.D_IN(data_in),.D_OUT(data_out),.DISK_READY(mounted),
      .DISK35(diskreg),.WRITE_PROTECT(1'b0),
      .WOZ_TRACK3_BIT_DATA(8'd0),.WOZ_TRACK3_BIT_COUNT(32'd0),.WOZ_TRACK3_READY(1'b0),
      .WOZ_TRACK3_DATA_VALID(1'b0),.WOZ_TRACK3_LOAD_COMPLETE(1'b0),.WOZ_TRACK3_IS_FLUX(1'b0),
      .WOZ_TRACK3_FLUX_SIZE(32'd0),.WOZ_TRACK3_FLUX_TOTAL_TICKS(32'd0),.WOZ_TRACK3_WP(1'b0),
      .WOZ_TRACK1_BIT_DATA(8'd0),.WOZ_TRACK1_BIT_COUNT(32'd0),.WOZ_TRACK1_DATA_VALID(1'b0),
      .WOZ_TRACK1_LOAD_COMPLETE(1'b0),.WOZ_TRACK1_IS_FLUX(1'b0),.WOZ_TRACK1_FLUX_SIZE(32'd0),
      .WOZ_TRACK1_FLUX_TOTAL_TICKS(32'd0),.WOZ_TRACK1_WP(1'b0)
    );

    task automatic bus(input [3:0] addr, input read_cycle, input [7:0] value, output [7:0] observed);
      @(negedge clk);
      offset=addr; selected=1; reading=read_cycle; data_in=value;
      #0.5 observed=data_out;
      @(negedge clk); selected=0;
    endtask

    task automatic touch(input [3:0] addr);
      reg [7:0] unused;
      bus(addr,1'b1,8'd0,unused);
    endtask

    task automatic check_status(input [7:0] mask, input [7:0] expected, input string label);
      reg [7:0] observed;
      touch(4'hd);
      bus(4'he,1'b1,8'd0,observed);
      if ((observed & mask) !== expected)
        $fatal(1,"%s expected=%02x actual=%02x mask=%02x",label,expected,observed,mask);
    endtask

    task automatic enable_mode(input [7:0] value);
      reg [7:0] unused;
      @(negedge clk); reset=1;
      repeat(4) @(negedge clk);
      reset=0;
      touch(4'hd);
      bus(4'hf,1'b0,value,unused);
      check_status(8'h1f,value,"IWM mode readback");
      touch(4'h9);
    endtask

    task automatic expect_delay(input integer delay_ticks);
      repeat(delay_ticks-6) @(negedge clk);
      check_status(8'h20,8'h20,"IWM motor-off timer still active");
      touch(4'h8); // Repeated off touches must not stall or rearm the timer.
      check_status(8'h20,8'h00,"IWM motor-off timer expired");
    endtask

    task automatic sony_motor_on;
      touch(4'h0); touch(4'h3); touch(4'h4); // CA0=0, CA1=1, CA2=0, SEL=0
      touch(4'h7); touch(4'h6);             // Strobe command $08
    endtask

    initial begin
      wait(run);
      enable_mode(8'h00);
      touch(4'h8); expect_delay(LONG_DELAY);
      enable_mode(8'h01);
      touch(4'h8); expect_delay(SHORT_DELAY);
      enable_mode(8'h05);
      touch(4'h8); check_status(8'h20,8'h00,"IWM immediate-off overrides LATCH");

      enable_mode(8'h01);
      touch(4'h8);
      repeat(SHORT_DELAY-30) @(negedge clk);
      touch(4'h9);
      repeat(40) @(negedge clk);
      check_status(8'h20,8'h20,"IWM enable cancels pending motor-off");
      touch(4'h8); expect_delay(SHORT_DELAY);

      mounted=4'b0100;
      enable_mode(8'h0f);
      sony_motor_on();
      check_status(8'ha0,8'h20,"Sony motor on");
      touch(4'h8); check_status(8'ha0,8'h80,"IWM disable masks Sony sense");
      touch(4'h9); check_status(8'ha0,8'h20,"IWM reenable restores Sony sense");
      touch(4'hb); check_status(8'ha0,8'ha0,"Select Sony drive 2");
      touch(4'ha); check_status(8'ha0,8'ha0,"Reselect Sony drive 1");
      sony_motor_on();
      touch(4'h8); diskreg[6]=0; check_status(8'ha0,8'h80,"Disabled, 35SEL clear");
      diskreg[6]=1; touch(4'ha); check_status(8'ha0,8'h80,"Disabled, 35SEL set");
      touch(4'h9); check_status(8'ha0,8'h20,"IWM reenable after disabled 35SEL toggle");
      diskreg[6]=0; check_status(8'ha0,8'ha0,"Enabled, 35SEL clear");
      diskreg[6]=1; touch(4'ha); check_status(8'ha0,8'ha0,"Enabled, 35SEL set again");
      $display("PASS: IWM motor-off timers and Sony motor selection via soft switches");
      $finish;
    end
endmodule

// Probe the actual production arithmetic tasks over every valid accumulator.
// No clock is driven in this top, so task results are isolated from the FSM.
module tb_apple_iigs_iwm_fraction;
    // The runner extracts these tasks and timing wires verbatim from production.
    // Separate wrappers avoid driving real FSM registers from this arithmetic probe.
    iwm_drive_fraction_probe drive();
    iwm_decoder_fraction_probe dut();
    integer sum_full=0,sum_half=0;
    initial begin
      integer full,half;
      for(integer frac=0;frac<1000;frac=frac+1)begin
        drive.bit_cell_frac=10'(frac);drive.bit_half_frac=10'(frac);
        dut.full_window_frac=10'(frac);dut.half_window_frac=10'(frac);
        full=frac+636;half=frac+318;
        drive.load_bit_timers();dut.load_full_window();#1;
        if(drive.bit_timer!=28+full/1000||drive.bit_cell_frac!=full%1000||
           drive.bit_half_timer!=14+half/1000||drive.bit_half_frac!=half%1000||
           dut.window_counter!=drive.bit_timer||dut.full_window_frac!=drive.bit_cell_frac)
          $fatal(1,"Full/drive fractional arithmetic mismatch frac=%0d",frac);
        sum_full=sum_full+int'(dut.window_counter);
        dut.load_half_window();#1;
        if(dut.window_counter!=drive.bit_half_timer||dut.half_window_frac!=drive.bit_half_frac)
          $fatal(1,"Half/drive fractional arithmetic mismatch frac=%0d",frac);
        sum_half=sum_half+int'(dut.window_counter);
      end
      if(sum_full!=28636||sum_half!=14318)$fatal(1,"Incorrect full/half arithmetic mean");
      $display("PASS: IWM all1000 fractional accumulators, full=%0d half=%0d",sum_full,sum_half);
      $finish;
    end
endmodule

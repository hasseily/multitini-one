// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps

// Original synthetic flux and CPU transactions. No system ROM or disk image.
module tb_apple_iigs_iwm;
    reg clk=0,reset=1;always #1 clk=~clk;
    reg [3:0] phase=0;
    always @(negedge clk)phase<=phase==13 ? 0:phase+1;
    integer scenario=0,poll_div=1,bus_cycles=0;
    always @(negedge clk)if(reset)bus_cycles<=0;else if(phase==13)bus_cycles<=bus_cycles+1;
    wire ce7=phase[0];
    reg manual_cen=0,manual_rd=0;reg [3:0] addr=4'hc;reg [7:0] mode=8'h0f;
    wire cen=scenario==0 ? phase==7:manual_cen;
    wire rd=scenario==0 ? (cen&&(bus_cycles%poll_div==0)):manual_rd;
    reg [7:0] track[0:65535];reg [7:0] bram_data=0;
    wire [15:0] bram_addr;wire drive_flux,spinning,ready;
    wire [16:0] position;wire [5:0] timer;
    integer bit_count=0,expected_count=0;
    reg [7:0] expected[0:4095];
    always @(posedge clk)bram_data<=track[bram_addr];
    flux_drive #(.SPINUP_BIT_COUNT(1)) drive(
      .IS_35_INCH(1'b1),.DRIVE_ID(2'd2),.CLK_14M(clk),.RESET(reset),
      .PHASES(4'h0),.IMMEDIATE_PHASES(4'h0),.LATCHED_SENSE_REG(3'd0),.IWM_MODE(mode[4:0]),
      .MOTOR_ON(1'b1),.SW_MOTOR_ON(1'b1),.DISKREG_SEL(1'b0),.SEL35(1'b1),.DRIVE_SELECT(1'b0),.DRIVE_SLOT(1'b0),
      .DISK_MOUNTED(1'b1),.DISK_WP(1'b0),.DOUBLE_SIDED(1'b1),.FLUX_TRANSITION(drive_flux),
      .MOTOR_SPINNING(spinning),.DRIVE_READY(ready),.BIT_POSITION(position),.BIT_TIMER_OUT(timer),
      .TRACK_BIT_COUNT(32'(bit_count)),.TRACK_LOADED(1'b1),.TRACK_LOAD_COMPLETE(1'b0),
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
    wire [7:0] dataout;wire bytevalid;
    iwm_flux dut(
      .CLK_14M(clk),.CLK_7M_EN(ce7),.RESET(reset),.CEN(cen),.ADDR(addr),.RD(rd),.WR(1'b0),.DATA_IN(8'd0),.DATA_OUT(dataout),
      .SW_PHASES(4'd0),.SW_MOTOR_ON(1'b1),.SW_DRIVE_SEL(1'b0),.SW_Q6(1'b0),.SW_Q7(1'b0),.SW_Q7_MODE(1'b0),.SW_MODE(mode),
      .FLUX_TRANSITION(flux),.MOTOR_ACTIVE(1'b1),.MOTOR_SPINNING(scenario==0 ? spinning:1'b1),
      .DISK_READY(scenario==0 ? ready:1'b1),.DISK_MOUNTED(1'b1),.IS_35_INCH(1'b1),
      .SENSE_BIT(1'b0),.LATCHED_SENSE_REG(3'd0),.DISKREG_SEL(1'b0),.DISK_BIT_POSITION(scenario==0 ? position:17'd0),
      .FLUX_BIT_TIMER(scenario==0 ? timer:6'd0),.SP_RD_DATA_VALID(1'b0),.SP_RD_DATA(8'd0),.SP_BSY(1'b0),.CBUS_ENABLE2(1'b0)
      ,.DEBUG_BYTE_VALID(bytevalid),.DEBUG_RSH(),.DEBUG_STATE()
    );
    task append_bit(input bit value);
      begin track[bit_count/8][7-bit_count%8]=value;bit_count=bit_count+1;end
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
      if(cycle>1000000)$fatal(1,"IWM regression timeout");
    end
    task access(input [3:0] a);
      begin
        @(negedge clk);addr=a;manual_cen=1;manual_rd=1;
        @(negedge clk);manual_cen=0;manual_rd=0;
        repeat(2)@(negedge clk);
      end
    endtask
    initial begin
      reg [7:0] first_byte;integer before_status;
      if(!$value$plusargs("SCENARIO=%d",scenario))scenario=0;
      if(!$value$plusargs("POLL_DIV=%d",poll_div))poll_div=1;
      for(integer i=0;i<65536;i=i+1)track[i]=0;
      // Long sync gaps plus all 128 high-bit byte values, with known prologues.
      for(integer group=0;group<8;group=group+1)begin
        for(integer i=0;i<8;i=i+1)append_byte(8'hff,1);
        append_byte(8'hd5,0);append_byte(8'haa,0);append_byte(8'h96,0);
        for(integer i=0;i<128;i=i+1)append_byte(8'h80|8'(i),0);
        append_byte(8'hd5,0);append_byte(8'haa,0);append_byte(8'had,0);
      end
      repeat(32)@(negedge clk);reset=0;
      force drive.sony_motor_on=1'b1;
      if(scenario!=0)begin
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

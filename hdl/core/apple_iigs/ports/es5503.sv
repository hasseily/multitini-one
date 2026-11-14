//============================================================================
//  Ensoniq ES5503 DOC (Digital Oscillator Chip)
//
//  32-oscillator wavetable sound generator, as fitted to the Apple IIgs with
//  64K of dedicated sound RAM.
//
//  Copyright (c) Steven A. Wilson
//
//  This program is free software; you can redistribute it and/or modify it
//  under the terms of the GNU General Public License as published by the Free
//  Software Foundation; either version 3 of the License, or (at your option)
//  any later version.
//
//  This program is distributed in the hope that it will be useful, but WITHOUT
//  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
//  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
//  more details.
//
//  You should have received a copy of the GNU General Public License along
//  with this program. If not, see <https://www.gnu.org/licenses/>.
//============================================================================

`timescale 1ns/1ns

// Multitini port, 2026-10-14: reproduced by scripts/port_apple_iigs_core.py.
// DOC interrupt FIFO from MiSTer IIgs aa2cc3e (slaapliedje).
module es5503
   (input	      CLK_14M,
    input	      clk_7M_en,
    input	      reset,
    input	      wr,
    input	      host_en,
    input [7:0]	      reg_addr,
    input [7:0]	      reg_data_in,
    input [7:0]	      sample_data_in,
    output reg [7:0]  data_out,
    output reg [16:0] addr_out,
    output reg [15:0] sound_out,
    output reg [3:0]  ca,
    output	      irq,
    output	      osc_en
    );

   // Global configuration
   reg [4:0]	     oscs_enabled;

   // Global state
   reg [4:0]	     current_osc;
   reg		     refreshing;
   reg		     current_refresh;

   // Per-oscillator configuration
   reg [7:0]	     r_freq_low    [31:0];
   reg [7:0]	     r_freq_high   [31:0];
   reg [7:0]	     r_volume      [31:0];
   reg [7:0]	     r_sample_data [31:0];
   reg [7:0]	     r_table_ptr   [31:0];
   reg [7:0]	     r_control     [31:0];
   reg [7:0]	     r_table_size  [31:0];

   // Oscillator state
   reg [23:0]	     accumulator [31:0];
   reg [31:0]        sample_zero;
   reg [31:0]        hold_data;
   reg [31:0]        final_fetch;


   // Accumulator adder / wrap detection
   wire [24:0]	     accumulator_sum;
   wire [24:0]	     accumulator_flip;
   wire		     accumulator_wrapped;

   reg [2:0]	     clk_phase;
   reg		     osc_en_d;

   // Convenience signals
   wire	[4:0]	     next_osc = current_osc + 5'b1;

   assign osc_en = (clk_phase == 3'b111) & clk_7M_en;

   always @(posedge CLK_14M) begin
      osc_en_d <= osc_en;
      if (clk_7M_en)
	clk_phase <= reset ? 3'b000 : clk_phase + 3'b001;
   end

   assign accumulator_sum = accumulator[current_osc] +
			    {9'b0,
			     r_freq_high[current_osc],
			     r_freq_low[current_osc]};

   assign accumulator_flip ={accumulator_sum[24],
			     accumulator_sum[23:0] ^ accumulator[current_osc]};

   assign accumulator_wrapped = accumulator_flip[17 + {2'b00, r_table_size[current_osc][2:0]}];

   wire [4:0] partner = current_osc ^ 5'd1;
   wire odd_swap_halted = !current_osc[0] && r_control[partner][2:0] == 3'b111;
   wire swap_can_start_partner = r_control[partner][0] &&
                                r_control[current_osc][2:1] == 2'd3;
   wire partner_zero_halted_free_run = r_control[partner][2:0] == 3'b001 &&
                                      sample_zero[partner];
   wire [7:0] oir;
   wire host_control_write = wr && host_en && reg_addr[7:5] == 3'b101;
   wire wave_end = osc_en && !refreshing && !r_control[current_osc][0] &&
                   (accumulator_wrapped || sample_zero[current_osc]);
   wire halted_zero = osc_en_d && !refreshing && r_control[current_osc][0] &&
                      sample_zero[current_osc] && sample_data_in == 8'h00;

   multitini_apple_iigs_doc_irq irq_controller(
       .clk(CLK_14M),
       .reset(reset),
       .slot(osc_en),
       .request(r_control[current_osc][3] && (wave_end || halted_zero)),
       .request_osc(current_osc),
       .cancel(host_control_write),
       .cancel_osc(reg_addr[4:0]),
       .ack(host_en && !wr && reg_addr == 8'he0),
       .ack_value(data_out),
       .oir(oir),
       .irq(irq)
   );

   integer	     i;

   task log_keyon (input [4:0] osc);
      $display("Key on: osc %d vol %h freq %h ptr %h size %h control %h",
	       osc, r_volume[osc], {r_freq_high[osc], r_freq_low[osc]},
	       r_table_ptr[osc], r_table_size[osc], r_control[osc]);
   endtask // log_keyon

   function [15:0] mux_addr (input [23:0] acc,
			     input [7:0]  tbl_ptr,
			     input [2:0]  tbl_index,
			     input [2:0]  res_index,
			     input	       halted);
      reg [7:0]				  ptr_mask;
      reg [15:0]			  acc_mask;
      reg [23:0]			  acc_shifted;
      reg [15:0]			  acc_bits;
      reg [15:0]			  ptr_bits;
      begin
	  ptr_mask = 8'hff << tbl_index;
	  acc_mask = (16'hffff >> (8 - tbl_index));
	  acc_shifted = acc >> (9 + res_index - tbl_index);
	  acc_bits = halted ? 16'd0 : acc_shifted[15:0] & acc_mask;
	  ptr_bits = ({8'h0, tbl_ptr} & {8'h0, ptr_mask[7:0]}) << 8;

	 mux_addr = acc_bits[15:0] | ptr_bits;
      end
   endfunction // mux_addr

   always @(posedge CLK_14M) begin
      addr_out <= {r_table_size[current_osc][6],
		   mux_addr(accumulator[current_osc],
			    r_table_ptr[current_osc],
			    r_table_size[current_osc][5:3],
			    r_table_size[current_osc][2:0],
			    r_control[current_osc][0] && !final_fetch[current_osc])};

      if (wr && host_en) begin
`ifdef DEBUG_SOUND
	 $display("%m: CPU write %h=%h", reg_addr, reg_data_in);
`endif
	 case (reg_addr[7:5])
	   0: begin
	      //if (current_osc == 0)
		//$display("freq_low[0] <= %h", reg_data_in);
	      r_freq_low[reg_addr[4:0]] <= reg_data_in;
	   end
	   1: begin r_freq_high[reg_addr[4:0]] <= reg_data_in;
	      //if (current_osc == 0)
		//$display("freq_high[0] <= %h", reg_data_in);
	   end
	   2: r_volume[reg_addr[4:0]] <= reg_data_in;
	   // r_sample_data; read-only
	   4: begin
	      r_table_ptr[reg_addr[4:0]] <= reg_data_in;
	      hold_data[reg_addr[4:0]] <= 1'b0;
	      final_fetch[reg_addr[4:0]] <= 1'b0;
	   end
	   5: begin
	      r_control[reg_addr[4:0]] <= reg_data_in;
	      if (reg_data_in[2:1] == 2'd1) begin
		 hold_data[reg_addr[4:0]] <= 1'b0;
		 final_fetch[reg_addr[4:0]] <= 1'b0;
	      end
	      else if (!r_control[reg_addr[4:0]][0] && reg_data_in[2:0] == 3'b001 &&
	               reg_addr[4:0] > oscs_enabled) begin
		 hold_data[reg_addr[4:0]] <= 1'b1;
		 final_fetch[reg_addr[4:0]] <= 1'b1;
	      end
	      if (r_control[reg_addr[4:0]][0] && !reg_data_in[0]) begin
		 accumulator[reg_addr[4:0]] <= 0;
		 // Don't halt on the next scan in case this was 8'h00
		 r_sample_data[reg_addr[4:0]] <= 'h80;
		 sample_zero[reg_addr[4:0]] <= 1'b0;
		 hold_data[reg_addr[4:0]] <= 1'b0;
		 final_fetch[reg_addr[4:0]] <= 1'b0;
		 //log_keyon(reg_addr[4:0]);
	      end
	   end
	   6: r_table_size[reg_addr[4:0]] <= reg_data_in;
	   7: if (reg_addr[4:0] == 5'h01)
		   oscs_enabled <= reg_data_in[5:1];
	 endcase // case (reg_addr[7:5])
      end // if (wr)
      else begin // read
	 case (reg_addr[7:5])
	   0: data_out <= r_freq_low[reg_addr[4:0]];
	   1: data_out <= r_freq_high[reg_addr[4:0]];
	   2: data_out <= r_volume[reg_addr[4:0]];
	   3: data_out <= r_sample_data[reg_addr[4:0]];
	   4: data_out <= r_table_ptr[reg_addr[4:0]];
	   5: data_out <= r_control[reg_addr[4:0]];
	   6: data_out <= {1'b1, r_table_size[reg_addr[4:0]][6:0]};
	   7: case (reg_addr[1:0])
		0: data_out <= oir;
		1: data_out <= {2'b11, oscs_enabled, 1'b1};
		// ADC; not implemented
	      endcase // case (reg_addr[1:0])
	 endcase // case (reg_addr[7:5])
      end // else: !if(wr)

      if (osc_en) begin
	 if (!refreshing && swap_can_start_partner &&
	     (wave_end || (r_control[current_osc][0] &&
	                   ((current_osc[0] && !partner_zero_halted_free_run) ||
	                    (!current_osc[0] && sample_zero[current_osc]))))) begin
	    r_control[partner][0] <= 1'b0;
	    r_sample_data[partner] <= 8'h80;
	    sample_zero[partner] <= 1'b0;
	    hold_data[partner] <= 1'b0;
	    final_fetch[partner] <= 1'b0;
	    accumulator[partner] <= 24'h0;
	 end
	 if (!r_control[current_osc][0] && !refreshing) begin
	    //$display("Osc %d running", current_osc);
	    // Accumulator/halt update
	    accumulator[current_osc] <= accumulator_sum[23:0];

	    // Halt the oscillator
	    if (accumulator_wrapped || sample_zero[current_osc]) begin
	       r_control[current_osc][0] <= sample_zero[current_osc] ||
		  (r_control[current_osc][1] &&
		   !(odd_swap_halted && r_control[current_osc][2:1] == 2'd1));
	       if (sample_zero[current_osc] &&
		   (r_control[current_osc][2:1] == 2'd0 || r_control[current_osc][2:1] == 2'd3))
		  hold_data[current_osc] <= 1'b1;

	       if (r_control[current_osc][1]) // oneshot or swap; clear the oscillator
		  accumulator[current_osc] <= 0;

	       if (r_control[current_osc][2]) begin // Sync/swap/AM
		  accumulator[current_osc] <= 0;

		  // A wrapping even sync oscillator resets its higher odd mate.
		  if (accumulator_wrapped && !current_osc[0] &&
		      r_control[current_osc][2:1] == 2'd2 &&
		      !r_control[current_osc ^ 5'h01][1])
		    accumulator[current_osc ^ 5'h01] <= 24'h0;

	       end

	    end // if (accumulator_wrapped || sample_zero[current_osc])

	    // Output sample update
	    if (r_control[current_osc][2:1] == 2'd2 && current_osc[0] == 1'd1) begin
	       // AM mode; no sample playback, just set the partner's volume
	       sound_out <= 16'h0000;
	       if (current_osc != 5'd31) // Oscillator 31 has no partner for AM
		 r_volume[next_osc] <= r_sample_data[current_osc] ^ 8'h80;
	    end
	    else if (sample_zero[current_osc])
	      sound_out <= 16'h0000;
	    else // All other modes
	      sound_out <= $signed(r_sample_data[current_osc] ^ 8'h80) *
			   $signed({8'b0, r_volume[current_osc]});

	    ca <= r_control[current_osc][7:4];
	 end // if (!r_control[current_osc][0])
	 else begin // Not running
	    sound_out <= 16'h0000;
	 end // else: !if(!r_control[current_osc][0])
      end // if (osc_en)
      else if (osc_en_d) begin
	 // We're using a synchronous sound RAM, so the data comes in one cycle later
	 if (!refreshing) begin
	    if (!r_control[current_osc][0])
	       sample_zero[current_osc] <= sample_data_in == 8'h00;
	    if (final_fetch[current_osc])
	       final_fetch[current_osc] <= 1'b0;
	    if (sample_data_in != 8'h00 &&
	        (!hold_data[current_osc] || final_fetch[current_osc]) &&
	        !(wr && host_en && reg_addr == {3'b101, current_osc} &&
	          r_control[current_osc][0] && !reg_data_in[0]))
	       r_sample_data[current_osc] <= sample_data_in;
	 end

	 if (current_osc == oscs_enabled && !refreshing) begin
	    refreshing <= 1;
	    current_refresh <= 0;
	 end
	 else if (refreshing) begin
	    if (current_refresh) begin
	       refreshing <= 0;
	       current_osc <= 0;
	    end
	    else
	      current_refresh <= 1;
	 end
	 else
	   current_osc <= next_osc;
      end // if (osc_en_d)

      if (reset) begin
	 current_osc <= 0;
	 refreshing <= 0;
	 current_refresh <= 0;
	 for (i=0; i < 32; i=i+1) accumulator[i] <= 0;
	 sample_zero <= 0;
	 hold_data <= 0;
	 final_fetch <= 0;
	 sound_out <= 16'h0000;
      end
   end // always @ (posedge clk)
endmodule // es5503

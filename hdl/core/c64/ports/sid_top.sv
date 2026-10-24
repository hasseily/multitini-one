// SPDX-License-Identifier: GPL-2.0-or-later
// Multitini compatibility port of MiSTer C64 commit
// 62fc697e3c9b5982c9dc76aa8cd03db545d20803. Procedurally assigned waveform
// arrays are variables, declarations precede use, reset initializes the shared
// scheduler, and dual-SID audio publishes only after the right MAC is valid.
module sid_top
#(
	parameter MULTI_FILTERS = 1,
	parameter DUAL = 1,
	parameter FAST_PORT = 0,
	parameter EDGE_STAGE = 1
)
(
	input         reset,

	input         clk,
	input         ce_1m,

	input [(DUAL ? 2 : 1)-1:0] cs,
	input         we,
	input   [4:0] addr,
	input   [7:0] data_in,
	output  [7:0] data_out,

	input [(DUAL ? 2 : 1)-1:0] fio_cs,
	input         fio_we,
	input         fio_latch,
	input [(DUAL ? 2 : 1)-1:0] fio_latch_src,
	input   [4:0] fio_addr,
	input   [7:0] fio_wdata,
	input [(DUAL ? 2 : 1)-1:0] fast_rd_cs,
	input   [4:0] fast_rd_addr,
	output logic [7:0] fast_rd_data,

	input  [12:0] fc_offset_l,
	input   [7:0] pot_x_l,
	input   [7:0] pot_y_l,
	input  [17:0] ext_in_l,
	output [17:0] audio_l,

	input  [12:0] fc_offset_r, // not used if DUAL is 0
	input   [7:0] pot_x_r,     // not used if DUAL is 0
	input   [7:0] pot_y_r,     // not used if DUAL is 0
	input  [17:0] ext_in_r,    // not used if DUAL is 0
	output [17:0] audio_r,

	input [(DUAL ? 2 : 1)-1:0] filter_en,
	input [(DUAL ? 2 : 1)-1:0] mode,
	input [((DUAL ? 2 : 1)*2)-1:0] cfg,

	input         ld_clk,
	input  [11:0] ld_addr,
	input  [15:0] ld_data,
	input         ld_wr
);

localparam N = DUAL ? 2 : 1;

// Internal Signals
reg  [15:0] Voice_1_Freq[N];
reg  [11:0] Voice_1_Pw[N];
reg   [7:0] Voice_1_Control[N];
reg   [7:0] Voice_1_Att_dec[N];
reg   [7:0] Voice_1_Sus_Rel[N];

reg  [15:0] Voice_2_Freq[N];
reg  [11:0] Voice_2_Pw[N];
reg   [7:0] Voice_2_Control[N];
reg   [7:0] Voice_2_Att_dec[N];
reg   [7:0] Voice_2_Sus_Rel[N];

reg  [15:0] Voice_3_Freq[N];
reg  [11:0] Voice_3_Pw[N];
reg   [7:0] Voice_3_Control[N];
reg   [7:0] Voice_3_Att_dec[N];
reg   [7:0] Voice_3_Sus_Rel[N];

reg  [10:0] Filter_Fc[N];
reg   [7:0] Filter_Res_Filt[N];
reg   [7:0] Filter_Mode_Vol[N];

wire  [7:0] Misc_Osc3[N];
wire  [7:0] Misc_Env3[N];

wire [21:0] voice_1[N];
wire [21:0] voice_2[N];
wire [21:0] voice_3[N];

wire        voice_1_PA_MSB[N];
wire        voice_2_PA_MSB[N];
wire        voice_3_PA_MSB[N];

logic [7:0] _st_out[N*3];
logic [7:0] p_t_out[N*3];
logic [7:0] ps__out[N*3];
logic [7:0] pst_out[N*3];
wire [11:0] acc_t[N*3];

reg  [17:0] audio[N];

reg   [7:0] bus_data[N];
reg  [12:0] Fc_offset[N];

wire  [7:0] f__st_out;
wire  [7:0] f_p_t_out;
wire  [7:0] f_ps__out;
wire  [7:0] f_pst_out;
reg  [11:0] f_acc_t;
reg   [3:0] state;
reg   [2:0] v;
reg  [17:0] audio0;
reg         state15_seen;
integer     audio_index;

generate
	genvar i;

	for(i=0; i<N; i=i+1) begin :chip
		reg [7:0] ctrl_stage[3];
		reg [2:0] ctrl_pending, ctrl_seen;
		wire fast_write = FAST_PORT && fio_cs[i] && fio_we && !cs[i];
		wire [7:0] fast_live = fio_addr == 5'h19 ? (i ? pot_x_r : pot_x_l) :
		                       fio_addr == 5'h1a ? (i ? pot_y_r : pot_y_l) :
		                       fio_addr == 5'h1b ? Misc_Osc3[i] :
		                       fio_addr == 5'h1c ? Misc_Env3[i] : bus_data[i];
		sid_voice v1
		(
			.clock(clk),
			.ce_1m(ce_1m),
			.reset(reset),
			.mode(mode[i]),
			.freq(Voice_1_Freq[i]),
			.pw(Voice_1_Pw[i]),
			.control(Voice_1_Control[i]),
			.att_dec(Voice_1_Att_dec[i]),
			.sus_rel(Voice_1_Sus_Rel[i]),
			.osc_msb_in(voice_3_PA_MSB[i]),
			.osc_msb_out(voice_1_PA_MSB[i]),
			.voice_out(voice_1[i]),
			._st_out(_st_out[i*3+0]),
			.p_t_out(p_t_out[i*3+0]),
			.ps__out(ps__out[i*3+0]),
			.pst_out(pst_out[i*3+0]),
			.acc_t(acc_t[i*3+0]),
			.osc_out(),
			.env_out()
		);

		sid_voice v2
		(
			.clock(clk),
			.ce_1m(ce_1m),
			.reset(reset),
			.mode(mode[i]),
			.freq(Voice_2_Freq[i]),
			.pw(Voice_2_Pw[i]),
			.control(Voice_2_Control[i]),
			.att_dec(Voice_2_Att_dec[i]),
			.sus_rel(Voice_2_Sus_Rel[i]),
			.osc_msb_in(voice_1_PA_MSB[i]),
			.osc_msb_out(voice_2_PA_MSB[i]),
			.voice_out(voice_2[i]),
			._st_out(_st_out[i*3+1]),
			.p_t_out(p_t_out[i*3+1]),
			.ps__out(ps__out[i*3+1]),
			.pst_out(pst_out[i*3+1]),
			.acc_t(acc_t[i*3+1]),
			.osc_out(),
			.env_out()
		);

		sid_voice v3
		(
			.clock(clk),
			.ce_1m(ce_1m),
			.reset(reset),
			.mode(mode[i]),
			.freq(Voice_3_Freq[i]),
			.pw(Voice_3_Pw[i]),
			.control(Voice_3_Control[i]),
			.att_dec(Voice_3_Att_dec[i]),
			.sus_rel(Voice_3_Sus_Rel[i]),
			.osc_msb_in(voice_2_PA_MSB[i]),
			.osc_msb_out(voice_3_PA_MSB[i]),
			.voice_out(voice_3[i]),
			.osc_out(Misc_Osc3[i]),
			.env_out(Misc_Env3[i]),
			._st_out(_st_out[i*3+2]),
			.p_t_out(p_t_out[i*3+2]),
			.ps__out(ps__out[i*3+2]),
			.pst_out(pst_out[i*3+2]),
			.acc_t(acc_t[i*3+2])
		);

		always @(posedge clk) Fc_offset[i] <= i ? fc_offset_r : fc_offset_l;

		// Register Decoding
		always @(posedge clk) begin
			if (reset) begin
				Voice_1_Freq[i]    <= 0;
				Voice_1_Pw[i]      <= 0;
				Voice_1_Control[i] <= 0;
				Voice_1_Att_dec[i] <= 0;
				Voice_1_Sus_Rel[i] <= 0;
				Voice_2_Freq[i]    <= 0;
				Voice_2_Pw[i]      <= 0;
				Voice_2_Control[i] <= 0;
				Voice_2_Att_dec[i] <= 0;
				Voice_2_Sus_Rel[i] <= 0;
				Voice_3_Freq[i]    <= 0;
				Voice_3_Pw[i]      <= 0;
				Voice_3_Control[i] <= 0;
				Voice_3_Att_dec[i] <= 0;
				Voice_3_Sus_Rel[i] <= 0;
				Filter_Fc[i]       <= 0;
				Filter_Res_Filt[i] <= 0;
				Filter_Mode_Vol[i] <= 0;
			end
			else if(cs[i]) begin
				if (we) begin
					bus_data[i] <= data_in;
					case (addr)
						5'h00: Voice_1_Freq[i][7:0] <= data_in;
						5'h01: Voice_1_Freq[i][15:8]<= data_in;
						5'h02: Voice_1_Pw[i][7:0]   <= data_in;
						5'h03: Voice_1_Pw[i][11:8]  <= data_in[3:0];
						5'h04: Voice_1_Control[i]   <= data_in;
						5'h05: Voice_1_Att_dec[i]   <= data_in;
						5'h06: Voice_1_Sus_Rel[i]   <= data_in;
						5'h07: Voice_2_Freq[i][7:0] <= data_in;
						5'h08: Voice_2_Freq[i][15:8]<= data_in;
						5'h09: Voice_2_Pw[i][7:0]   <= data_in;
						5'h0a: Voice_2_Pw[i][11:8]  <= data_in[3:0];
						5'h0b: Voice_2_Control[i]   <= data_in;
						5'h0c: Voice_2_Att_dec[i]   <= data_in;
						5'h0d: Voice_2_Sus_Rel[i]   <= data_in;
						5'h0e: Voice_3_Freq[i][7:0] <= data_in;
						5'h0f: Voice_3_Freq[i][15:8]<= data_in;
						5'h10: Voice_3_Pw[i][7:0]   <= data_in;
						5'h11: Voice_3_Pw[i][11:8]  <= data_in[3:0];
						5'h12: Voice_3_Control[i]   <= data_in;
						5'h13: Voice_3_Att_dec[i]   <= data_in;
						5'h14: Voice_3_Sus_Rel[i]   <= data_in;
						5'h15: Filter_Fc[i][2:0]    <= data_in[2:0];
						5'h16: Filter_Fc[i][10:3]   <= data_in;
						5'h17: Filter_Res_Filt[i]   <= data_in;
						5'h18: Filter_Mode_Vol[i]   <= data_in;
					endcase
				end
				else begin
					case (addr)
						5'h19: bus_data[i] = i ? pot_x_r : pot_x_l;
						5'h1a: bus_data[i] = i ? pot_y_r : pot_y_l;
						5'h1b: bus_data[i] = Misc_Osc3[i];
						5'h1c: bus_data[i] = Misc_Env3[i];
					endcase
				end
			end else if (FAST_PORT && fio_cs[i]) begin
				if (fio_we) begin
					bus_data[i] <= (fio_addr >= 5'h19 && fio_addr <= 5'h1c) ? fast_live : fio_wdata;
					case (fio_addr)
						5'h00: Voice_1_Freq[i][7:0] <= fio_wdata;
						5'h01: Voice_1_Freq[i][15:8]<= fio_wdata;
						5'h02: Voice_1_Pw[i][7:0]   <= fio_wdata;
						5'h03: Voice_1_Pw[i][11:8]  <= fio_wdata[3:0];
						5'h04: if (!EDGE_STAGE) Voice_1_Control[i]   <= fio_wdata;
						5'h05: Voice_1_Att_dec[i]   <= fio_wdata;
						5'h06: Voice_1_Sus_Rel[i]   <= fio_wdata;
						5'h07: Voice_2_Freq[i][7:0] <= fio_wdata;
						5'h08: Voice_2_Freq[i][15:8]<= fio_wdata;
						5'h09: Voice_2_Pw[i][7:0]   <= fio_wdata;
						5'h0a: Voice_2_Pw[i][11:8]  <= fio_wdata[3:0];
						5'h0b: if (!EDGE_STAGE) Voice_2_Control[i]   <= fio_wdata;
						5'h0c: Voice_2_Att_dec[i]   <= fio_wdata;
						5'h0d: Voice_2_Sus_Rel[i]   <= fio_wdata;
						5'h0e: Voice_3_Freq[i][7:0] <= fio_wdata;
						5'h0f: Voice_3_Freq[i][15:8]<= fio_wdata;
						5'h10: Voice_3_Pw[i][7:0]   <= fio_wdata;
						5'h11: Voice_3_Pw[i][11:8]  <= fio_wdata[3:0];
						5'h12: if (!EDGE_STAGE) Voice_3_Control[i]   <= fio_wdata;
						5'h13: Voice_3_Att_dec[i]   <= fio_wdata;
						5'h14: Voice_3_Sus_Rel[i]   <= fio_wdata;
						5'h15: Filter_Fc[i][2:0]    <= fio_wdata[2:0];
						5'h16: Filter_Fc[i][10:3]   <= fio_wdata;
						5'h17: Filter_Res_Filt[i]   <= fio_wdata;
						5'h18: Filter_Mode_Vol[i]   <= fio_wdata;
					endcase
				end else if (fio_latch) begin
					bus_data[i] <= fio_latch_src[i] ? fio_wdata : fast_live;
				end
			end

			if (FAST_PORT && EDGE_STAGE) begin
				if (reset || (cs[i] && we && addr == 5'h04)) begin
					ctrl_pending[0] <= 0;
					ctrl_seen[0] <= 0;
				end else if (ce_1m) begin
					if (ctrl_pending[0]) begin
						Voice_1_Control[i] <= ctrl_stage[0];
						ctrl_seen[0] <= 1;
						ctrl_pending[0] <= fast_write && fio_addr == 5'h04;
						if (fast_write && fio_addr == 5'h04) ctrl_stage[0] <= fio_wdata;
					end else if (fast_write && fio_addr == 5'h04) begin
						Voice_1_Control[i] <= fio_wdata;
						ctrl_pending[0] <= 0;
						ctrl_seen[0] <= 1;
					end else begin
						ctrl_pending[0] <= 0;
						ctrl_seen[0] <= 0;
					end
				end else if (fast_write && fio_addr == 5'h04) begin
					if (ctrl_seen[0]) begin
						ctrl_stage[0] <= fio_wdata;
						ctrl_pending[0] <= 1;
					end else begin
						Voice_1_Control[i] <= fio_wdata;
						ctrl_seen[0] <= 1;
					end
				end
			end

			if (FAST_PORT && EDGE_STAGE) begin
				if (reset || (cs[i] && we && addr == 5'h0b)) begin
					ctrl_pending[1] <= 0;
					ctrl_seen[1] <= 0;
				end else if (ce_1m) begin
					if (ctrl_pending[1]) begin
						Voice_2_Control[i] <= ctrl_stage[1];
						ctrl_seen[1] <= 1;
						ctrl_pending[1] <= fast_write && fio_addr == 5'h0b;
						if (fast_write && fio_addr == 5'h0b) ctrl_stage[1] <= fio_wdata;
					end else if (fast_write && fio_addr == 5'h0b) begin
						Voice_2_Control[i] <= fio_wdata;
						ctrl_pending[1] <= 0;
						ctrl_seen[1] <= 1;
					end else begin
						ctrl_pending[1] <= 0;
						ctrl_seen[1] <= 0;
					end
				end else if (fast_write && fio_addr == 5'h0b) begin
					if (ctrl_seen[1]) begin
						ctrl_stage[1] <= fio_wdata;
						ctrl_pending[1] <= 1;
					end else begin
						Voice_2_Control[i] <= fio_wdata;
						ctrl_seen[1] <= 1;
					end
				end
			end

			if (FAST_PORT && EDGE_STAGE) begin
				if (reset || (cs[i] && we && addr == 5'h12)) begin
					ctrl_pending[2] <= 0;
					ctrl_seen[2] <= 0;
				end else if (ce_1m) begin
					if (ctrl_pending[2]) begin
						Voice_3_Control[i] <= ctrl_stage[2];
						ctrl_seen[2] <= 1;
						ctrl_pending[2] <= fast_write && fio_addr == 5'h12;
						if (fast_write && fio_addr == 5'h12) ctrl_stage[2] <= fio_wdata;
					end else if (fast_write && fio_addr == 5'h12) begin
						Voice_3_Control[i] <= fio_wdata;
						ctrl_pending[2] <= 0;
						ctrl_seen[2] <= 1;
					end else begin
						ctrl_pending[2] <= 0;
						ctrl_seen[2] <= 0;
					end
				end else if (fast_write && fio_addr == 5'h12) begin
					if (ctrl_seen[2]) begin
						ctrl_stage[2] <= fio_wdata;
						ctrl_pending[2] <= 1;
					end else begin
						Voice_3_Control[i] <= fio_wdata;
						ctrl_seen[2] <= 1;
					end
				end
			end
		end
	end
endgenerate

// Independent side-effect-free sampling. Native reads keep their original latch path.
integer fast_index;
always_comb begin
	fast_rd_data = 8'hff;
	if (FAST_PORT) begin
		for (fast_index = N-1; fast_index >= 0; fast_index = fast_index-1) begin
			if (fast_rd_cs[fast_index]) begin
				case (fast_rd_addr)
				5'h19: fast_rd_data = fast_index ? pot_x_r : pot_x_l;
				5'h1a: fast_rd_data = fast_index ? pot_y_r : pot_y_l;
				5'h1b: fast_rd_data = Misc_Osc3[fast_index];
				5'h1c: fast_rd_data = Misc_Env3[fast_index];
				default: fast_rd_data = bus_data[fast_index];
				endcase
			end
		end
	end
end


wire [15:0] F0;
wire n = DUAL && state[3];

sid_tables #(MULTI_FILTERS) sid_tables
(
	.clock(clk),
	.mode(mode[n]),

	.acc_t(f_acc_t),
	._st_out(f__st_out),
	.p_t_out(f_p_t_out),
	.ps__out(f_ps__out),
	.pst_out(f_pst_out),

	.cfg(cfg[n*2 +:2]),
	.Fc(Filter_Fc[n]),
	.Fc_offset(Fc_offset[n]),
	.F0(F0),
	.ld_clk(ld_clk),
	.ld_addr(ld_addr),
	.ld_data(ld_data),
	.ld_wr(ld_wr)
);

always @(posedge clk) begin
	if (reset) begin
		state <= 0;
		f_acc_t <= 0;
		v <= 0;
	end else begin
		if(~&state) state <= state + 1'd1;
		if(ce_1m) state <= 0;

		case(state)
			1,3,5,7,9,11: begin
				f_acc_t <= acc_t[state[3:1]];
				v <= state[3:1];
			end
		endcase

		case(state)
			3,5,7: begin
				_st_out[v] <= f__st_out;
				p_t_out[v] <= f_p_t_out;
				ps__out[v] <= f_ps__out;
				pst_out[v] <= f_pst_out;
			end
			9,11,13: if(DUAL) begin
				_st_out[v] <= f__st_out;
				p_t_out[v] <= f_p_t_out;
				ps__out[v] <= f_ps__out;
				pst_out[v] <= f_pst_out;
			end
		endcase
	end
end

wire [17:0] faudio;

sid_filter sid_filter
(
	.clk(clk),
	.state(state[2:0]),
	.mode(mode[n]),

	.F0(F0),
	.Res_Filt(Filter_Res_Filt[n]),
	.Mode_Vol(Filter_Mode_Vol[n]),
	.voice1(voice_1[n]),
	.voice2(voice_2[n]),
	.voice3(voice_3[n]),
	.ext_in(22'(signed'({n ? ext_in_r : ext_in_l, 3'b000}))),

	.audio(faudio)
);

always @(posedge clk) begin
	if (reset) begin
		audio0 <= 0;
		state15_seen <= 0;
		for (audio_index = 0; audio_index < N; audio_index = audio_index + 1)
			audio[audio_index] <= 0;
	end else begin
		// faudio is two states later than upstream (was 6) because
		// sid_filter pipelines the compressor across filter states 5-7.
		// The first result is valid at state 8. The second audio MAC is only
		// launched by the first state-15 edge, so publishing on that same edge
		// briefly exposed the preceding filter intermediate as the right
		// channel. Wait for one repeated state-15 edge and publish both channels
		// together as one coherent pair.
		state15_seen <= (state == 15) && !ce_1m;
		if(state == 8)
			audio0 <= faudio;
		if(state == 15 && state15_seen) begin
			audio[n] <= faudio;
			audio[0] <= audio0;
		end
	end
end

assign data_out = cs[0] ? bus_data[0] : bus_data[N-1];

assign audio_l = audio[0];
assign audio_r = audio[N-1];

endmodule

// Multitini ECS port; reproduced by scripts/port_amiga_core.py.
////////////////////////////////////////////////////////////////////////////////
//                                                                            //
// Copyright 2006, 2007 Dennis van Weeren                                     //
//                                                                            //
// This file is part of Minimig                                               //
//                                                                            //
// Minimig is free software; you can redistribute it and/or modify            //
// it under the terms of the GNU General Public License as published by       //
// the Free Software Foundation; either version 3 of the License, or          //
// (at your option) any later version.                                        //
//                                                                            //
// Minimig is distributed in the hope that it will be useful,                 //
// but WITHOUT ANY WARRANTY; without even the implied warranty of             //
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the              //
// GNU General Public License for more details.                               //
//                                                                            //
// You should have received a copy of the GNU General Public License          //
// along with this program.  If not, see <http://www.gnu.org/licenses/>.      //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////
//                                                                            //
// Agnus beamcounter                                                          //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////


module agnus_beamcounter
(
	input	            clk,            // bus clock
	input             clk7_en,
	input	            reset,          // reset
	input	            cck,            // CCK clock
	input	            ntsc,           // NTSC mode switch
	input             aga,            
	input	            ecs,            // ECS enable switch
	input	            a1k,            // enable A1000 VBL interrupt timing
	input	     [15:0] data_in,        // bus data in
	output reg [15:0] data_out,       // bus data out
	input       [8:1] reg_address_in, // register address inputs
	output reg  [8:0] hpos,           // horizontal beam counter (140ns)
	output reg [10:0] vpos,           // vertical beam counter
	output reg        _hsync,         // horizontal sync
	output reg        _vsync,         // vertical sync
	output            field1,         // 
	output reg        lace,
	output            _csync,         // composite sync
	output reg        hblank,         // video blanking
	output reg        vblank,         // video blanking
	output            vbl,            // vertical blanking
	output            vblend,         // last line of vertival blanking
	output            eol,            // end of video line
	output            eof,            // end of video frame
	output reg        vbl_int,        // vertical interrupt request (for Paula)
	output      [8:0] htotal_out,     // video line length
	output            harddis_out,
	output            varbeamen_out
);
// Strict-tool declaration order; no functional change.
parameter VPOSR    = 9'h004;
parameter VPOSW    = 9'h02A;
parameter VHPOSR   = 9'h006;
parameter VHPOSW   = 9'h02C;
parameter BPLCON0  = 9'h100;
parameter HTOTAL   = 9'h1C0;
parameter HSSTOP   = 9'h1C2;
parameter HBSTRT   = 9'h1C4;
parameter HBSTOP   = 9'h1C6;
parameter VTOTAL   = 9'h1C8;
parameter VSSTOP   = 9'h1CA;
parameter VBSTRT   = 9'h1CC;
parameter VBSTOP   = 9'h1CE;
parameter HSSTRT   = 9'h1DE;
parameter BEAMCON0 = 9'h1DC;
parameter VSSTRT   = 9'h1E0;
parameter HCENTER  = 9'h1E2;
parameter HBSTRT_VAL      = 17+4+4;
parameter HSSTRT_VAL      = 29+4+4;
parameter HSSTOP_VAL      = 63-1+4+4;
parameter HBSTOP_VAL      = 103-5+4;
parameter HCENTER_VAL     = 256+4+4;
parameter VSSTRT_VAL      = 2;
parameter VSSTOP_VAL      = 5;
parameter VBSTRT_VAL      = 0;
parameter HTOTAL_VAL      = 8'd227 - 8'd1;
parameter VTOTAL_PAL_VAL  = 11'd312 - 11'd1;
parameter VTOTAL_NTSC_VAL = 11'd262 - 11'd1;
parameter VBSTOP_PAL_VAL  = 9'd25;
parameter VBSTOP_NTSC_VAL = 9'd20;
reg [10:0] vpos_rb;
reg [10:0] vpos_d1;
reg [15:0] beamcon0_reg;
wire harddis;
wire varvben;
wire loldis;
wire varvsyen;
wire varhsyen;
wire varbeamen;
reg ersy;
reg pal;
reg [ 8:0] htotal_reg;
reg [ 8:0] hsstrt_reg;
reg [ 8:0] hsstop_reg;
reg [ 8:0] hcenter_reg;
reg [ 8:0] hbstrt_reg;
reg [ 8:0] hbstop_reg;
reg [ 1:0] hbstrt_lo;
reg [ 1:0] hbstop_lo;
reg [10:0] vtotal_reg;
reg [10:0] vsstrt_reg;
reg [10:0] vsstop_reg;
reg [10:0] vbstop_reg;
wire [ 8:0] htotal;
wire [ 8:0] hsstrt;
wire [ 8:0] hsstop;
wire [ 8:0] hcenter;
wire [ 8:0] hbstrt;
wire [ 8:0] hbstop;
wire [ 1:0] hbstrt_f;
wire [ 1:0] hbstop_f;
wire [10:0] vtotal;
wire [10:0] vsstrt;
wire [10:0] vsstop;
wire [10:0] vbstop;
reg end_of_line;
reg long_line;
reg vpos_inc;
reg long_frame;
wire vpos_equ_vtotal;
reg extra_line;
wire last_line;
wire end_of_frame;
reg vser;
reg  [2:0] hbs_d, hbe_d;
wire [3:0] hbs_s;
wire [3:0] hbe_s;
assign harddis = beamcon0_reg[14];
assign varvben = beamcon0_reg[12];
assign loldis = beamcon0_reg[11];
assign varvsyen = beamcon0_reg[ 9];
assign varhsyen = beamcon0_reg[ 8];
assign varbeamen = beamcon0_reg[ 7];
assign htotal =             varbeamen ? htotal_reg  : HTOTAL_VAL << 1;
assign hsstrt = varhsyen && varbeamen ? hsstrt_reg  : HSSTRT_VAL[8:0];
assign hsstop = varhsyen && varbeamen ? hsstop_reg  : HSSTOP_VAL[8:0];
assign hcenter = varhsyen && varbeamen ? hcenter_reg : HCENTER_VAL[8:0];
assign hbstrt =             varbeamen ? hbstrt_reg  : HBSTRT_VAL[8:0];
assign hbstop =             varbeamen ? hbstop_reg  : HBSTOP_VAL[8:0];
assign hbstrt_f =            varbeamen ? hbstrt_lo   : 2'b00;
assign hbstop_f =            varbeamen ? hbstop_lo   : 2'b00;
assign vtotal =             varbeamen ? vtotal_reg  : pal ? VTOTAL_PAL_VAL : VTOTAL_NTSC_VAL;
assign vsstrt = varvsyen && varbeamen ? vsstrt_reg  : VSSTRT_VAL[10:0];
assign vsstop = varvsyen && varbeamen ? vsstop_reg  : VSSTOP_VAL[10:0];
assign vbstop = varvben  && varbeamen ? vbstop_reg  : pal ? VBSTOP_PAL_VAL : VBSTOP_NTSC_VAL;
assign vpos_equ_vtotal = (vpos==vtotal);
assign last_line = long_frame ? extra_line : vpos_equ_vtotal;
assign end_of_frame = vpos_inc & last_line;
assign hbs_s = {hbs_d, clk7_en && hpos==hbstrt};
assign hbe_s = {hbe_d, clk7_en && hpos==hbstop};


//register names and adresses		


















          // horizontal blanking start
          // front porch = 1.6us (29)
        // hsync pulse duration = 4.7us (63)
         // back porch = 4.7us (103) shorter blanking for overscan visibility
         // position of vsync pulse during the long field of interlaced screen
               // vertical sync start
               // PAL vsync width: 2.5 lines (NTSC: 3 lines - not implemented)
               // vertical blanking start
   // line length of 227 CCKs in PAL mode (NTSC line length of 227.5 CCKs is not supported)
 // total number of lines (PAL: 312 lines, NTSC: 262)
 // total number of lines (PAL: 312 lines, NTSC: 262)
           // vertical blanking end (PAL 26 lines, NTSC vblank 21 lines)
           // vertical blanking end (PAL 26 lines, NTSC vblank 21 lines)

//wire	[8:0] vbstop;		// vertical blanking stop

//beam position output signals
//assign	htotal = 8'd227 - 8'd1;                           // line length of 227 CCKs in PAL mode (NTSC line length of 227.5 CCKs is not supported)
//assign	vtotal = pal ? VTOTAL_PAL_VAL : VTOTAL_NTSC_VAL;  // total number of lines (PAL: 312 lines, NTSC: 262)
//assign	vbstop = pal ? VBSTOP_PAL_VAL : VBSTOP_NTSC_VAL;  // vertical blanking end (PAL 26 lines, NTSC vblank 21 lines)

//first visible line $1A (PAL) or $15 (NTSC)
//sprites are fetched on line $19 (PAL) or $14 (NTSC) - vblend signal used to tell Agnus to fetch sprites during the last vertical blanking line

//--------------------------------------------------------------------------------------



always @(posedge clk) begin
	if (clk7_en) begin
		vpos_d1 <= vpos;
		vpos_rb <= vpos_d1;
	end
end

//beamcounter read registers VPOSR and VHPOSR
always @(*) begin
	if (reg_address_in[8:1]==VPOSR[8:1] || reg_address_in[8:1]==VPOSW[8:1])
		data_out[15:0] = {long_frame,1'b0,ecs,ntsc,2'b00,{2{aga}},long_line,4'b0000,vpos_rb[10:8]};
	else if (reg_address_in[8:1]==VHPOSR[8:1] || reg_address_in[8:1]==VHPOSW[8:1])
		data_out[15:0] = {vpos_rb[7:0],|hpos[8:1] ? hpos[8:1] - 8'd1 : ersy ? 8'd0 : htotal[8:1]};
	else
		data_out[15:0] = 0;
end

// BEAMCON0 register

always @ (posedge clk) begin
	if (clk7_en) begin
		if (reset)
			beamcon0_reg <= {10'b0, ~ntsc, 5'b0};
		else if ((reg_address_in[8:1] == BEAMCON0[8:1]) && ecs)
			beamcon0_reg <= data_in[15:0];
	end
end


//wire lpendis      = beamcon0_reg[13];


//wire cscben       = beamcon0_reg[10];



//wire displaydual  = beamcon0_reg[ 6];
//wire displaypal   = beamcon0_reg[ 5];
//wire varcsyen     = beamcon0_reg[ 4];
//wire blanken      = beamcon0_reg[ 3];
//wire csynctrue    = beamcon0_reg[ 2];
//wire vsynctrue    = beamcon0_reg[ 1];
//wire hsynctrue    = beamcon0_reg[ 0];


// write ERSY bit of bplcon0 register (External ReSYnchronization - genlock)

always @(posedge clk) begin
	if (clk7_en) begin
		if (reset)
			ersy <= 1'b0;
		else if (reg_address_in[8:1] == BPLCON0[8:1])
			ersy <= data_in[1];
	end
end

//BPLCON0 register
always @(posedge clk) begin
	if (clk7_en) begin
		if (reset)
			lace <= 1'b0;
		else if (reg_address_in[8:1]==BPLCON0[8:1])
			lace <= data_in[2];
	end
end

//BEAMCON0 register
	// pal mode switch
always @(posedge clk) begin
	if (clk7_en) begin
		if (reset)
			pal <= ~ntsc;
		else if (reg_address_in[8:1]==BEAMCON0[8:1] && ecs)
			pal <= data_in[5];
	end
end

// programmable display mode regs











//reg [10:0] vbstrt_reg;


always @ (posedge clk) begin
	if (clk7_en) begin
		if (reset) begin
			htotal_reg  <= HTOTAL_VAL << 1;
			hsstrt_reg  <= HSSTRT_VAL[8:0];
			hsstop_reg  <= HSSTOP_VAL[8:0];
			hcenter_reg <= HCENTER_VAL[8:0];
			hbstrt_reg  <= HBSTRT_VAL[8:0];
			hbstop_reg  <= HBSTOP_VAL[8:0];
			hbstrt_lo   <= 2'b00;
			hbstop_lo   <= 2'b00;
			vtotal_reg  <= pal ? VTOTAL_PAL_VAL : VTOTAL_NTSC_VAL;
			vsstrt_reg  <= VSSTRT_VAL[10:0];
			vsstop_reg  <= VSSTOP_VAL[10:0];
			//vbstrt_reg  <= VBSTRT_VAL[10:0];
			vbstop_reg  <= pal ? VBSTOP_PAL_VAL : VBSTOP_NTSC_VAL;
		end else begin
			case (reg_address_in[8:1])
				HTOTAL [8:1] : htotal_reg  <= {data_in[ 7:0], 1'b0};
				HSSTRT [8:1] : hsstrt_reg  <= {data_in[ 7:0], 1'b0};
				HSSTOP [8:1] : hsstop_reg  <= {data_in[ 7:0], 1'b0};
				HCENTER[8:1] : hcenter_reg <= {data_in[ 7:0], 1'b0};
				HBSTRT [8:1] : {hbstrt_reg, hbstrt_lo} <= {data_in[7:0], aga ? data_in[10:8] : 3'b000};
				HBSTOP [8:1] : {hbstop_reg, hbstop_lo} <= {data_in[7:0], aga ? data_in[10:8] : 3'b000};
				VTOTAL [8:1] : vtotal_reg  <= {data_in[10:0]};
				VSSTRT [8:1] : vsstrt_reg  <= {data_in[10:0]};
				VSSTOP [8:1] : vsstop_reg  <= {data_in[10:0]};
				//VBSTRT [8:1] : vbstrt_reg  <= {data_in[10:0]};
				VBSTOP [8:1] : vbstop_reg  <= {data_in[10:0]};
			endcase
		end
	end
end

// programmable display mode values
 // line length of 227 CCKs in PAL mode (NTSC line length of 227.5 CCKs is not supported)










//wire [10:0] vbstrt  = varvben  && varbeamen ? vbstrt_reg  : VBSTRT_VAL[10:0];


assign htotal_out    = htotal;
assign harddis_out   = harddis || varbeamen || varvben;
assign varbeamen_out = varbeamen;


//--------------------------------------------------------------------------------------//
//                                                                                      //
//   HORIZONTAL BEAM COUNTER                                                            //
//                                                                                      //
//--------------------------------------------------------------------------------------//

//generate start of line signal

always @(posedge clk) begin
	if (clk7_en) begin
		if (hpos[8:0]=={htotal[8:1],1'b0})
			end_of_line <= 1'b1;
		else
			end_of_line <= 1'b0;
	end
end

// horizontal beamcounter
always @(posedge clk) begin
	if (clk7_en) begin
		if (reg_address_in[8:1]==VHPOSW[8:1])
			hpos[8:1] <= data_in[7:0]; 
		else if (end_of_line)
			hpos[8:1] <= 0;
		else if (cck && (~ersy || |hpos[8:1]))
			hpos[8:1] <= hpos[8:1] + 1'b1;
	end
end

always @(cck) hpos[0] = cck;

//long line signal (not used, only for better NTSC compatibility)
	 // long line signal for NTSC compatibility (actually long lines are not supported yet)
always @(posedge clk) begin
	if (clk7_en) begin
		if (end_of_line)
			if (pal || (loldis && varbeamen))
				long_line <= 1'b0;
			else if (!(loldis && varbeamen))
				long_line <= ~long_line;
	end
end

//--------------------------------------------------------------------------------------//
//                                                                                      //
//   VERTICAL BEAM COUNTER                                                              //
//                                                                                      //
//--------------------------------------------------------------------------------------//

//vertical counter increase
 // increase vertical position counter
always @(posedge clk) begin
	if (clk7_en) begin
		if (hpos==2) //actual chipset works in this way
			vpos_inc <= 1'b1;
		else
			vpos_inc <= 1'b0;
	end
end

//external signals assigment
assign eol = vpos_inc;

//vertical position counter
//vpos changes after hpos equals 3
always @(posedge clk) begin
	if (clk7_en) begin
		if (reg_address_in[8:1]==VPOSW[8:1])
			vpos[10:8] <= data_in[2:0];
		else if (reg_address_in[8:1]==VHPOSW[8:1])
			vpos[7:0] <= data_in[15:8];
		else if (vpos_inc)
			if (last_line)
				vpos <= 0;
			else
				vpos <= vpos + 1'b1;
	end
end

// long_frame - long frame signal used in interlaced mode
 // 1 : long frame (313 lines); 0 : normal frame (312 lines)
always @(posedge clk) begin
	if (clk7_en) begin
		if (reset)
			long_frame <= 1'b1;
		else if (reg_address_in[8:1]==VPOSW[8:1])
			long_frame <= data_in[15];
		else if (end_of_frame && lace) // interlace
			long_frame <= ~long_frame;
	end
end

//maximum position of vertical beam position
 // vertical beam counter is equal to its maximum count (in interlaced mode it counts one line more)

//extra line in interlaced mode	
 // extra line (used in interlaced mode)
always @(posedge clk) begin
	if (clk7_en) begin
		if (vpos_inc)
			if (long_frame && vpos_equ_vtotal)
				extra_line <= 1'b1;
			else
				extra_line <= 1'b0;
	end
end

//in non-interlaced display the last line is equal to vtotal or vtotal+1 (depends on long_frame)
//in interlaced mode every second frame is vtotal+1 long


assign field1 = (~long_frame) & lace;

//generate end of frame signal


//external signal assigment
assign eof = end_of_frame;

always @(posedge clk) if (clk7_en) vbl_int <= hpos==8 && vpos==(a1k ? 1 : 0); // OCS AGNUS CHIPS 8361/8367 assert vbl int in line #1

//--------------------------------------------------------------------------------------//
//                                                                                      //
//  VIDEO SYNC GENERATOR                                                                //
//                                                                                      //
//--------------------------------------------------------------------------------------//

//horizontal sync
always @(posedge clk) begin
	if (clk7_en) begin
		if (hpos==hsstrt)//start of sync pulse (front porch = 1.69us)
			_hsync <= 1'b0;
		else if (hpos==hsstop)//end of sync pulse (sync pulse = 4.65us)
			_hsync <= 1'b1;
	end
end

//vertical sync and vertical blanking
// PAL: Long field Vsync line 3 - 5.5, Short field: line 2.5 - 5
always @(posedge clk) begin
	if (clk7_en) begin
		if ((vpos==vsstrt+1 && hpos==hsstrt && long_frame) || (vpos==vsstrt && hpos==hcenter && !long_frame))
			_vsync <= 1'b0;
		else if ((vpos==vsstop && hpos==hcenter && long_frame) || (vpos==vsstop && hpos==hsstrt && !long_frame))
			_vsync <= 1'b1;		
	end
end

//apparently generating csync from vsync alligned with leading edge of hsync results in malfunction of the AD724 CVBS/S-Video encoder (no colour in interlaced mode)
//to overcome this limitation semi (only present before horizontal sync pulses) vertical sync serration pulses are inserted into csync
 // vertical sync serration pulses for composite sync
always @(posedge clk) begin //sync
	if (clk7_en) begin
		if (hpos==hsstrt-(hsstop-hsstrt))//start of sync pulse (front porch = 1.69us)
			vser <= 1'b1;
		else if (hpos==hsstrt)//end of sync pulse	(sync pulse = 4.65us)
			vser <= 1'b0;
	end
end

//composite sync
assign _csync = _hsync & _vsync | vser; //composite sync with serration pulses

//--------------------------------------------------------------------------------------//
//                                                                                      //
//  VIDEO BLANKING GENERATOR                                                            //
//                                                                                      //
//--------------------------------------------------------------------------------------//

/*
//vertical blanking
reg vbl_reg;
always @ (posedge clk) begin
  if (reset)
    vbl_reg <= 1'b0;
  else if (vpos == vbstrt)
    vbl_reg <= 1'b1;
  else if (vpos == vbstop)
    vbl_reg <= 1'b0;
end

assign vbl = vbl_reg; // TODO
*/

assign vbl = (vpos <= vbstop);

//vertical blanking end (last line)
assign vblend = vpos==vbstop;

//composite display blanking




always @(posedge clk) begin
	hbs_d <= hbs_s[2:0];
	hbe_d <= hbe_s[2:0];
	if (hbs_s[hbstrt_f])       //start of blanking (active line=51.88us)
		hblank <= 1;
	else if (hbe_s[hbstop_f]) begin //end of blanking (back porch=5.78us)
		vblank <= vbl;
		hblank <= 0;
	end
end

endmodule

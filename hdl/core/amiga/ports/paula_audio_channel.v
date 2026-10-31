// Multitini ECS port; reproduced by scripts/port_amiga_core.py.
//This module handles a single amiga audio channel. attached modes are not supported
// 2020-09-07: pwm controlled volume gated sample output implemented by OKK

module paula_audio_channel
(
  input   clk,          //bus clock
  input clk7_en,
  input  cck,          //colour clock enable
  input   reset,            //reset
  input  aen,          //address enable
  input  dmaena,          //dma enable
  input  [3:1] reg_address_in,    //register address input
  input   [15:0] data,       //bus data input
  output  [6:0] volume,      //channel volume output
  output  [7:0] sample,      //channel sample output
  output  [7:0] sample_okk,  //channel sample output (PWM gated by volumectr like suggested in the HW manual)
  output  intreq,          //interrupt request
  input  intpen,          //interrupt pending input
  output  reg dmareq,        //dma request
  output  reg dmas,        //dma special (restart)
  input  strhor          //horizontal strobe
);
// Strict-tool declaration order; no functional change.
parameter  AUDLEN = 4'h4;
parameter  AUDPER = 4'h6;
parameter  AUDVOL = 4'h8;
parameter  AUDDAT = 4'ha;
parameter AUDIO_STATE_0 = 3'b000;
parameter AUDIO_STATE_1 = 3'b001;
parameter AUDIO_STATE_2 = 3'b011;
parameter AUDIO_STATE_3 = 3'b010;
parameter AUDIO_STATE_4 = 3'b110;
reg    [15:0] audlen;
reg    [15:0] audper;
reg    [6:0] audvol;
reg    [15:0] auddat;
reg    [15:0] datbuf;
reg    [2:0] audio_state;
reg    [2:0] audio_next;
wire   datwrite;
reg    [5:0] volcnt;
wire   volcount;
reg    volcntrld;
wire   volgate;
reg    pbufld1;
reg    [15:0] percnt;
reg    percount;
reg    percntrld;
wire  perfin;
reg    [15:0] lencnt;
reg    lencount;
reg    lencntrld;
wire  lenfin;
reg   AUDxDAT;
wire  AUDxON;
reg    AUDxDR;
reg    AUDxIR;
wire  AUDxIP;
reg    intreq2_set;
reg    intreq2_clr;
reg    intreq2;
reg    dmasen;
reg    penhi;
reg silence;
reg silence_d;
reg dmaena_d;


//register names and addresses





//local signals
      //audio length register
      //audio period register
      //audio volume register
      //audio data register

      //audio data buffer
    //audio current state
     //audio next state

        //data register is written

	   // pwm volume counter
	      // increase pwm volume counter
       // not used (BS! Oh yes it's used!!!)
		   // pwm pulse for gating sample output

        //load output sample from sample buffer

      //audio period counter
        //decrease period counter
        //reload period counter
          //period counter expired

      //audio length counter
        //decrease length counter
        //reload length counter
          //length counter expired

        //audio data buffer was written
          //audio DMA channel is enabled
          //audio DMA request
          //audio interrupt request
          //audio interrupt is pending



        //buffered interrupt request

          //pointer register reloading request
          //enable high byte of sample buffer

  // AMR: disable audio if repeat length is 1
  // AMR: disable audio if repeat length is 1



//length register bus write
always @(posedge clk) begin
  if (clk7_en) begin
    if (reset)
      audlen[15:0] <= 16'h00_00;
    else if (aen && (reg_address_in[3:1]==AUDLEN[3:1]))
      audlen[15:0] <= data[15:0];
  end
end

//period register bus write
always @(posedge clk) begin
  if (clk7_en) begin
    if (reset)
      audper[15:0] <= 16'h00_00;
    else if (aen && (reg_address_in[3:1]==AUDPER[3:1]))
      audper[15:0] <= data[15:0];
  end
end

//volume register bus write
always @(posedge clk) begin
  if (clk7_en) begin
    if (reset)
      audvol[6:0] <= 7'b000_0000;
    else if (aen && (reg_address_in[3:1]==AUDVOL[3:1]))
      audvol[6:0] <= data[6:0];
  end
end

//data register strobe
assign datwrite = (aen && (reg_address_in[3:1]==AUDDAT[3:1])) ? 1'b1 : 1'b0;

//data register bus write
always @(posedge clk) begin
  if (clk7_en) begin
    if (reset)
      auddat[15:0] <= 16'h00_00;
    else if (datwrite)
      auddat[15:0] <= data[15:0];
  end
end

always @(posedge clk) begin
  if (clk7_en) begin
    if (datwrite)
      AUDxDAT <= 1'b1;
    else if (cck)
      AUDxDAT <= 1'b0;
  end
end

assign  AUDxON = dmaena;  //dma enable

assign  AUDxIP = intpen;  //audio interrupt pending

assign intreq = AUDxIR;    //audio interrupt request


//volume counter
always @(posedge clk) begin
  if (clk7_en) begin
    if (reset && cck) volcnt <= 6'h00;

    else if (volcntrld && cck)    // load volume counter from audio volume register
      volcnt[5:0] <= audvol[5:0];
    else if (volcount && cck)     // volume counter count up
      volcnt[5:0] <= volcnt[5:0] - 6'd1;
  end
end

assign volcount = 1'd1;
assign volgate = (volcnt < audvol);

//period counter
always @(posedge clk) begin
  if (clk7_en) begin
    if (percntrld && cck)//load period counter from audio period register
      percnt[15:0] <= audper[15:0];
    else if (percount && cck)//period counter count down
      percnt[15:0] <= percnt[15:0] - 16'd1;
  end
end

assign perfin = (percnt[15:0]==1 && cck) ? 1'b1 : 1'b0;

//length counter
always @(posedge clk) begin
  if (clk7_en) begin
    if (lencntrld && cck) begin //load length counter from audio length register
      lencnt[15:0] <= (audlen[15:0]);
      silence<=1'b0;
      if(audlen==1 || audlen==0)
         silence<=1'b1;
    end else if (lencount && cck)//length counter count down
      lencnt[15:0] <= (lencnt[15:0] - 1'd1);
    // Silence fix
    dmaena_d<=dmaena;
    if(dmaena_d==1'b1 && dmaena==1'b0) begin
      silence_d<=1'b1; // Prevent next write from unsilencing the channel.
      silence<=1'b1;
    end
    if(AUDxDAT && cck)  // Unsilence the channel if the CPU writes to AUDxDAT
      if(silence_d)
        silence_d<=1'b0;
      else
        silence<=1'b0;
  end
end

assign lenfin = (lencnt[15:0]==1 && cck) ? 1'b1 : 1'b0;


//audio buffer
always @(posedge clk) begin
  if (clk7_en) begin
    if (reset)
      datbuf[15:0] <= 16'h00_00;
    else if (pbufld1 && cck)
      datbuf[15:0] <= auddat[15:0];
  end
end

//assign sample[7:0] = penhi ? datbuf[15:8] : datbuf[7:0];
assign sample[7:0] = silence ? 8'b0 : (penhi ? datbuf[15:8] : datbuf[7:0]);

// By: OKK (The correct way Paula really works!)
assign sample_okk[7:0] = (silence | !volgate ? 8'b0 : (penhi ? datbuf[15:8] : datbuf[7:0])); // pwm volume gated sample output

//volume output
assign volume[6:0] = audvol[6:0];

//dma request logic
always @(posedge clk) begin
  if (clk7_en) begin
    if (reset)
    begin
      dmareq <= 1'b0;
      dmas <= 1'b0;
    end
    else if (AUDxDR && cck)
    begin
      dmareq <= 1'b1;
      dmas <= dmasen | lenfin;
    end
    else if (strhor) //dma request are cleared when transfered to Agnus
    begin
      dmareq <= 1'b0;
      dmas <= 1'b0;
    end
  end
end

//buffered interrupt request
always @(posedge clk) begin
  if (clk7_en) begin
    if (cck)
      if (intreq2_set)
        intreq2 <= 1'b1;
      else if (intreq2_clr)
        intreq2 <= 1'b0;
  end
end

//audio states






//audio channel state machine
always @(posedge clk) begin
  if (clk7_en) begin
    if (reset)
      audio_state <= AUDIO_STATE_0;
    else if (cck)
      audio_state <= audio_next;
  end
end

//transition function
always @(*) begin
  case (audio_state)

    AUDIO_STATE_0: //audio FSM idle state
    begin
      intreq2_clr = 1'b1;
      intreq2_set = 1'b0;
      lencount = 1'b0;
      penhi = 1'b0;
      percount = 1'b0;
      percntrld = 1'b1;

      if (AUDxON) //start of DMA driven audio playback
      begin
        audio_next = AUDIO_STATE_1;
        AUDxDR = 1'b1;
        AUDxIR = 1'b0;
        dmasen = 1'b1;
        lencntrld = 1'b1;
        pbufld1 = 1'b0;
        volcntrld = 1'b0;
      end
      else if (AUDxDAT && !AUDxON && !AUDxIP)  //CPU driven audio playback
      begin
        audio_next = AUDIO_STATE_3;
        AUDxDR = 1'b0;
        AUDxIR = 1'b1;
        dmasen = 1'b0;
        lencntrld = 1'b0;
        pbufld1 = 1'b1;
        volcntrld = 1'b1;
      end
      else
      begin
        audio_next = AUDIO_STATE_0;
        AUDxDR = 1'b0;
        AUDxIR = 1'b0;
        dmasen = 1'b0;
        lencntrld = 1'b0;
        pbufld1 = 1'b0;
        volcntrld = 1'b0;
      end
    end

    AUDIO_STATE_1: //audio DMA has been enabled
    begin
      dmasen = 1'b0;
      intreq2_clr = 1'b1;
      intreq2_set = 1'b0;
      lencntrld = 1'b0;
      penhi = 1'b0;
      percount = 1'b0;

      if (AUDxON && AUDxDAT) //requested data has arrived
      begin
        audio_next = AUDIO_STATE_2;
        AUDxDR = 1'b1;
        AUDxIR = 1'b1;
        lencount = ~lenfin;
        pbufld1 = 1'b0;  //first data received, discard it since first data access is used to reload pointer
        percntrld = 1'b0;
        volcntrld = 1'b0;
      end
      else if (!AUDxON) //audio DMA has been switched off so go to IDLE state
      begin
        audio_next = AUDIO_STATE_0;
        AUDxDR = 1'b0;
        AUDxIR = 1'b0;
        lencount = 1'b0;
        pbufld1 = 1'b0;
        percntrld = 1'b0;
        volcntrld = 1'b0;
      end
      else
      begin
        audio_next = AUDIO_STATE_1;
        AUDxDR = 1'b0;
        AUDxIR = 1'b0;
        lencount = 1'b0;
        pbufld1 = 1'b0;
        percntrld = 1'b0;
        volcntrld = 1'b0;
      end
    end

    AUDIO_STATE_2: //audio DMA has been enabled
    begin
      dmasen = 1'b0;
      intreq2_clr = 1'b1;
      intreq2_set = 1'b0;
      lencntrld = 1'b0;
      penhi = 1'b0;
      percount = 1'b0;

      if (AUDxON && AUDxDAT) //requested data has arrived
      begin
        audio_next = AUDIO_STATE_3;
        AUDxDR = 1'b1;
        AUDxIR = 1'b0;
        lencount = ~lenfin;
        pbufld1 = 1'b1;  //new data has been just received so put it in the output buffer
        percntrld = 1'b1;
        volcntrld = 1'b1;
      end
      else if (!AUDxON) //audio DMA has been switched off so go to IDLE state
      begin
        audio_next = AUDIO_STATE_0;
        AUDxDR = 1'b0;
        AUDxIR = 1'b0;
        lencount = 1'b0;
        pbufld1 = 1'b0;
        percntrld = 1'b0;
        volcntrld = 1'b0;
      end
      else
      begin
        audio_next = AUDIO_STATE_2;
        AUDxDR = 1'b0;
        AUDxIR = 1'b0;
        lencount = 1'b0;
        pbufld1 = 1'b0;
        percntrld = 1'b0;
        volcntrld = 1'b0;
      end
    end

    AUDIO_STATE_3: //first sample is being output
    begin
      AUDxDR = 1'b0;
      AUDxIR = 1'b0;
      dmasen = 1'b0;
      intreq2_clr = 1'b0;
      intreq2_set = lenfin & AUDxON & AUDxDAT;
      lencount = ~lenfin & AUDxON & AUDxDAT;
      lencntrld = lenfin & AUDxON & AUDxDAT;
      pbufld1 = 1'b0;
      penhi = 1'b1;
      volcntrld = 1'b0;

      if (perfin) //if period counter expired output other sample from buffer
      begin
        audio_next = AUDIO_STATE_4;
        percount = 1'b0;
        percntrld = 1'b1;
      end
      else
      begin
        audio_next = AUDIO_STATE_3;
        percount = 1'b1;
        percntrld = 1'b0;
      end
    end

    AUDIO_STATE_4: //second sample is being output
    begin
      dmasen = 1'b0;
      intreq2_set = lenfin & AUDxON & AUDxDAT;
      lencount = ~lenfin & AUDxON & AUDxDAT;
      lencntrld = lenfin & AUDxON & AUDxDAT;
      penhi = 1'b0;
      volcntrld = 1'b0;

      if (perfin && (AUDxON || !AUDxIP)) //period counter expired and audio DMA active
      begin
        audio_next = AUDIO_STATE_3;
        AUDxDR = AUDxON;
        AUDxIR = (intreq2 & AUDxON) | ~AUDxON;
        intreq2_clr = intreq2;
        pbufld1 = 1'b1;
        percount = 1'b0;
        percntrld = 1'b1;
      end
      else if (perfin && !AUDxON && AUDxIP) //period counter expired and audio DMA inactive
      begin
        audio_next = AUDIO_STATE_0;
        AUDxDR = 1'b0;
        AUDxIR = 1'b0;
        intreq2_clr = 1'b0;
        pbufld1 = 1'b0;
        percount = 1'b0;
        percntrld = 1'b0;
      end
      else
      begin
        audio_next = AUDIO_STATE_4;
        AUDxDR = 1'b0;
        AUDxIR = 1'b0;
        intreq2_clr = 1'b0;
        pbufld1 = 1'b0;
        percount = 1'b1;
        percntrld = 1'b0;
      end
    end

    default:
    begin
      audio_next = AUDIO_STATE_0;
      AUDxDR = 1'b0;
      AUDxIR = 1'b0;
      dmasen = 1'b0;
      intreq2_clr = 1'b0;
      intreq2_set = 1'b0;
      lencntrld = 1'b0;
      lencount = 1'b0;
      pbufld1 = 1'b0;
      penhi = 1'b0;
      percount = 1'b0;
      percntrld = 1'b0;
      volcntrld = 1'b0;
    end

  endcase
end


endmodule


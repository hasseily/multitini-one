/* 
 * File:   VectrexSound.cpp
 * Author: Bob
 * 
 * Created on May 10, 2016, 7:57 PM
 */
#include <stdlib.h>

#include "VectrexSound.h"
#include "../Array.h"
// ROM music, envelopes and frequency bytes are supplied by the caller.
// Only the MIT sequencer implementation is retained in this port.
extern "C" unsigned mt_v32_note_frequency(unsigned note);

VectrexMusicPlayer::VectrexMusicPlayer(VMusic *m) : Vec_Music_Flag(1), isExplosion(0)
{
  music = new VMusic();  
  // ATTENTION: no deep copy!
  music->adsr = m->adsr;  // pointer to 16 byte
  music->twang = m->twang; // pointer to 16 byte
  music->size = m->size; // pointer to 16 byte
  music->music = new uint8_t[music->size+2];
  for (int i=0;i<music->size; i++)
    music->music[i] = m->music[i];
  
  for (int i=0;i<16;i++)
  {
    Vec_ADSR_Table[i] = m->adsr[i];
    Vec_Twang_Table[i] = m->twang[i];
  }
}


VectrexMusicPlayer::VectrexMusicPlayer(const Value &notes, unsigned char* a, signed char *t) : Vec_Music_Flag(1), isExplosion(0)
{
  music = new VMusic();  
  music->adsr = a;  // pointer to 16 byte
  music->twang = t; // pointer to 16 byte
  for (int i=0;i<16;i++)
  {
    Vec_ADSR_Table[i] = a[i];
    Vec_Twang_Table[i] = t[i];
  }

  // Figure out how much space we need
  size_t size = 0;
  const uint16_t *dims = notes.Dimensions();
  for (uint16_t i = 0; i < dims[0]; ++i)
  {
      size += 2; // A channel note and duration
      // The high 8 bits are 0, 1, or 2 indicating 1, 2, or 3 channels
      size += notes[i * 2].Integer() >> 24;
  }
  // Add the end bytes
  size += 2;
  music->size = size; // 

  // Reserve space for the music table and two end bytes
  uint8_t *data = new uint8_t[size + 2];
  uint8_t *dest = data;

  int channels = 0;
  for (uint16_t i = 0; i < dims[0]; ++i)
  {
      // Get the notes value, which can combine up to 3 channels
      channels = notes[i * 2].Integer();
      // Get the number of channels
      int count = (channels >> 24) + 1;
      // Write note(s) for the channel(s). The last channel has bit7 clear,
      // the previous channels need bit7 set
      int channelShift = channels;
      while (count--)
      {
//printf("%02x ",uint8_t((channelShift & 0x7f) | (count ? 128 : 0)));
          *dest++ = uint8_t((channelShift & 0x7f) | (count ? 128 : 0));
          channelShift >>= 8;
      }
      // Write the duration
      *dest++ = uint8_t(notes[i * 2 + 1].Integer());
//printf("%02x \n\r",uint8_t(notes[i * 2 + 1].Integer()));
  }
  // To end the music, reuse the last note and use a terminator for the duration
  *dest = *(dest - 2);
  *++dest = 0x80;
  music->music = data;
  
}

void VectrexMusicPlayer::Init_Music()
{
  
 int ch;
 if (!Vec_Music_Flag) return; // no music
 if (Vec_Music_Flag==1) // new Music
 {
//   printf ("Init Player \n\r");
//Init_Music:     LDX     #Freq_Table     ;Save pointer to frequency table
//                STX     <Vec_Freq_Table
//Init_Music_dft: LDA     #$80            ;Set sound active flag
//                STA     <Vec_Music_Flag
//   LDD     ,U++            ;Save address of ADSR table
//                STD     <Vec_ADSR_Table
//                LDD     ,U++            ;Save address of twang table
//                STD     <Vec_Twang_Table
//                STU     <Vec_Music_Ptr  ;Save pointer to music data
   Vec_Music_Flag = -1;
   Vec_Music_Ptr = music->music;
//                JSR     Init_Music_Buf  ;Initialize music buffer
   v_noSound();
//                LDD     #$1F1F
//                STD     <Vec_ADSR_Timers+1;Init ADSR timers of chans 2 & 3
//                LDD     #$0000
//                STD     <Vec_Music_Freq+2;Clear frequency of channel 2
//                STD     <Vec_Music_Freq+4;Clear frequency of channel 3
//                STA     <Vec_Music_Chan ;A-reg = 0 (sound channel number?)
   Vec_Music_Twang_Count[0] = Vec_Music_Twang_Count[1] = Vec_Music_Twang_Count[2] = 0;
   Vec_Music_Twang_Value[0] = Vec_Music_Twang_Value[1] = Vec_Music_Twang_Value[2] = 0;
   Vec_ADSR_Timers[0] =0x1f;
   Vec_ADSR_Timers[1] =0x1f;
   Vec_ADSR_Timers[2] =0x1f;
   Vec_Music_Freq[0] = 0;
   Vec_Music_Freq[1] = 0;
   Vec_Music_Freq[2] = 0;
   Vec_Music_Chan=0;
   Vec_Duration = 0;
//                BRA     LF6EC
 }
 else // continue music
 {
//   printf ("Music playing: \n\r");
   
   
//LF6B3:          LDU     #Vec_ADSR_Timers;Get address of ADSR timers
//                LDB     #$02            ;Count for three channels
   for (ch=2; ch>=0;ch--)
   {
//LF6B8:          LDA     B,U             ;Get the channel's ADSR timer
//                CMPA    #$1F
//                BEQ     LF6C0           ;Skip if at maximum
//                INC     B,U             ;Else increment the timer
      if (Vec_ADSR_Timers[ch] != 0x1f) // not maximum
      {
        Vec_ADSR_Timers[ch]++;
      }
   }
//LF6C0:          DECB                    ;Go back for the other channels
//                BPL     LF6B8

   for (ch=0; ch<3;ch++)
   {
//                LDU     #Vec_Music_Twang
//                LDA     #$07            ;Twang limit is 6-8 depending on channel
//LF6CA:          INC     ,U              ;Increment twang counter
//                CMPA    ,U              ;Check against limit
//                BGE     LF6D2
//                CLR     ,U              ;Clear it if limit exceeded
if (++Vec_Music_Twang_Count[ch] > ch+7) Vec_Music_Twang_Count[ch] = 0;

//                LDX     <Vec_Twang_Table
//LF6D2:          LDB     ,U+             ;Get twang count
//                ANDB    #$07            ;Mask out low 3 bits
//                LDB     B,X             ;Get twang value from table
//                STB     ,U+             ;Update current twang value
Vec_Music_Twang_Value[ch] = Vec_Twang_Table[(Vec_Music_Twang_Count[ch])&7];
   
//                INCA                    ;Increment twang limit
//                CMPA    #$09
//                BLS     LF6CA           ;Go back until all three channels done
   }
//  DEC     <Vec_Duration   ;Decrement the duration timer
//                BNE     LF74E           ;Update ADSR while note still playing
   Vec_Duration--;
   if (Vec_Duration>=0) goto LF74E;

//LF6E3:          LDA     <Vec_Music_Chan ;Go to next music channel
//                DECA
//                BPL     LF6EA           ;If < 0, set it to 2
//                LDA     #$02
//LF6EA:          STA     <Vec_Music_Chan
   Vec_Music_Chan--;
   if (Vec_Music_Chan<0) Vec_Music_Chan=2;
   
 }
 
 while (1)
 {
   

//LF6EC:          LDB     [Vec_Music_Ptr] ;Get next byte of music data
//                LDU     #Vec_ADSR_Timers;Clear ADSR timer for this channel
//                CLR     A,U
  unsigned char b = *Vec_Music_Ptr;   
//  printf ("Music byte: %02x\n\r", b);
//  printf ("(b=$%02x)", b);
  Vec_ADSR_Timers[Vec_Music_Chan] = 0;

//                BITB    #$40            ;If $40 bit of music data set,
//                BEQ     LF712           ;we're going to make some noise
  if (b&0x40)
  {
//    printf ("Noise\n\r");
    
    // noise
    //                ; Continue currently playing sound here
    //                LDX     #Music_Table_1  ;Get bit mask for this channel
    //                LDA     A,X
    //                ANDA    <Vec_Music_Wk_7 ;Turn channel bit off for register 7
    //                STA     <Vec_Music_Wk_7
    v_writePSG_double_buffered(7, ~(8u << Vec_Music_Chan) & v_readPSG_double_buffered(7));
    //                LDA     <Vec_Music_Chan ;Set current channel bit in register 7
    //                ADDA    #$03
    //                LDA     A,X
    //                ORA     <Vec_Music_Wk_7
    //                STA     <Vec_Music_Wk_7
    //                ANDB    #$1F            ;Mask off low 5 bits of music data
    //                STB     <Vec_Music_Wk_6 ;and store in register 6

//printf ("%01x:$%02x, ",7,  (1u << Vec_Music_Chan) | v_readPSG_double_buffered(7));
    v_writePSG_double_buffered(7,  (1u << Vec_Music_Chan) | v_readPSG_double_buffered(7));
//printf ("%01x:$%02x, ", 6,  (b&0x1f));
    v_writePSG_double_buffered(6,  b&0x1f);
  }
  else
  {
//    printf ("Sound\n\r");
      // sound
    //LF712:          LDX     #Music_Table_2  ;If $40 bit of music data was cleared,
    //                LDA     A,X             ;Get bit mask for this channel
    //                ANDA    <Vec_Music_Wk_7 ;Turn channel bit off for register 7
    //                STA     <Vec_Music_Wk_7
    v_writePSG_double_buffered(7, ~(1u << Vec_Music_Chan) & v_readPSG_double_buffered(7));
            
                    
    //                LDA     <Vec_Music_Chan ;Set current channel bit in register 7
    //                ADDA    #$03
    //                LDA     A,X
    //                ORA     <Vec_Music_Wk_7
    //                STA     <Vec_Music_Wk_7
//printf ("%01x:$%02x, ",7,  (8u << Vec_Music_Chan) | v_readPSG_double_buffered(7));
    v_writePSG_double_buffered(7,  (8u << Vec_Music_Chan) | v_readPSG_double_buffered(7));


    //                LDA     <Vec_Music_Chan ;Get $C855 * 2 + 3
    //                ASLA
    //                ADDA    #Vec_Music_Freq-Vec_ADSR_Timers
    //                LEAU    A,U             ;Point U-reg to #$C861 + $C855 * 2
    //                ANDB    #$3F            ;Mask off low 6 bits of music data,
    //                ASLB                    ;multiply by 2
    //                LDX     <Vec_Freq_Table ;Get pointer to note-to-frequency table
    //                LDD     B,X             ;Get note table data

                    
    //                STD     ,U              ;Store in word at $C861-$C866
    Vec_Music_Freq[Vec_Music_Chan] = mt_v32_note_frequency(b & 0x3f);
    //((mt_v32_note_frequency(b & 0x3f)>>8) & 0xff)  +  (((mt_v32_note_frequency(b & 0x3f)) & 0xff)<<8); // to little endian!
  }
 

//LF735:          LDX     <Vec_Music_Ptr  ;Re-get byte of music data
//                LDB     ,X+
//                STX     <Vec_Music_Ptr  ;Update music data pointer
  b = *Vec_Music_Ptr++;   
//  printf ("(b=$%02x)", b);

  if (b&0x80)
  {
//                TSTB
//                BMI     LF6E3           ;If byte>=$80, advance to next channel
   Vec_Music_Chan--;
   if (Vec_Music_Chan<0) Vec_Music_Chan=2;
   continue;    
  }
                 
//                LDB     ,X+             ;Get second byte of music data
  b = *Vec_Music_Ptr++;   
//  printf ("(b=$%02x)", b);
//  printf ("Music byte: %02x\n\r", b);
//                BPL     LF748           ;If >=$80, (terminator)
//                JSR     Init_Music_Buf  ;  clear music buffer,
//                CLR     <Vec_Music_Flag ;  clear music flag,
//                RTS                     ;  and exit
  if (b&0x80)
  {
    // music finished completely!
    stop();
//printf ("Music END\n\r", b);
    return;
  }
//LF748:          STX     <Vec_Music_Ptr  ;Update music data pointer
//                ANDB    #$3F            ;Duration in low 6 bits of second byte
//                STB     <Vec_Duration   ;Store duration counter
  Vec_Duration = b&0x3f;
  break;
  
}   
  
  
  
LF74E:  
  for (ch = 2;ch>=0;ch--)
  {
    

//LF74E:          LDY     <Vec_ADSR_Table ;Get pointer to ADSR table
//                LDU     #Vec_ADSR_Timers;Point to ADSR timer table
//                LDX     #Vec_Music_Wk_A
//                LDA     #$02            ;Count for three channels
//LF759:          LDB     ,U+             ;Get channel timer?
//                BITB    #$01            ;Test low bit of ADSR index
//                BEQ     LF766
//                LSRB                    ;If odd, divide ADSR index by by 2
//                LDB     B,Y             ;Get low nibble from ADSR table
//                ANDB    #$0F
//                BRA     LF76D
//
//LF766:          LSRB                    ;If even, divide ADSR index by 2
//                LDB     B,Y             ;Get high nibble from ADSR table
//                LSRB
//                LSRB
//                LSRB
//                LSRB

  unsigned char nibble = Vec_ADSR_Table[Vec_ADSR_Timers[ch]>>1];
  if ((Vec_ADSR_Timers[ch] & 1) == 0)
    nibble = nibble>>4;
  nibble = nibble &0x0f;
  
//LF76D:          STB     A,X             ;Store ADSR value in regs 10-12
//                DECA                    ;Decrement channel counter
//                BPL     LF759           ;Go back for next channel
  v_writePSG_double_buffered(8+ch, nibble); // volume is decoded in ADSR nibbles
//printf ("%01x:$%02x, ", 8+ch, nibble);
  }
//unsigned char Vec_Music_Twang_Count[3];
//unsigned char Vec_Music_Twang_Value[3];
                
                
//                LDU     #Vec_Music_Freq+6;Point to base frequency table
//                LDX     #Vec_Music_Wk_5 ;Point to twang table
//LF778:          LDD     ,--U            ;Get next base frequency
//                TST     -8,U            ;Test twang value
//                BPL     LF788

 for (ch = 0;ch <3; ch++)
 {
  unsigned int baseFreq = Vec_Music_Freq[ch];
//                NEG     -8,U            ;If <0, negate twang table entry
//                SUBB    -8,U            ;Subtract negated value from frequency
//                SBCA    #$00            ;Propagate borrow to high byte
//                NEG     -8,U            ;Un-negate twang entry
//                BRA     LF78C
//LF788:          ADDB    -8,U            ;If >0 add twang to base frequency
//                ADCA    #$00            ;Propagate carry to high byte
//LF78C:          STD     ,X++            ;Store freq in regs 5/4, 3/2, 1/0
//                CMPX    #Vec_Music_Work+14
//                BNE     LF778
//LF793_RTS:      RTS

  baseFreq +=Vec_Music_Twang_Value[ch];
//printf ("%01x:$%02x, ", ch*2+0, baseFreq&0xff);
  v_writePSG_double_buffered(ch*2+0, baseFreq&0xff );
//printf ("%01x:$%02x, ",ch*2+1, (baseFreq>>8)&0x0f );
  v_writePSG_double_buffered(ch*2+1, (baseFreq>>8)&0x0f  );
 }

// printf ("\n\r----\n\r");

}




// I know this verbatim 
// asm copy could be
// easily beautyfied
// but I really can't be bothered
// for more info on used registers and their meaning
// look at the header file
void VectrexMusicPlayer::playExplosion()
{
 unsigned char a;
 unsigned char b;


//Explosion_Snd:  LDA     <Vec_Expl_Flag
//                BPL     LF95B

  if (Vec_Expl_Flag<0)
  {
    
//                ANDA    #$7F
//                STA     <Vec_Expl_Flag
  Vec_Expl_Flag= (Vec_Expl_Flag & 0x7f); // makeit positive
//                LDX     #Vec_Expl_1     ;Copy 4 bytes from U-reg to $C858
//                LDA     #$04
//                JSR     Move_Mem_a
  Vec_Expl_ChanB = Vec_Expl_ChanA = Vec_Expl_Chans = Vec_Expl_1[0] = byte0;                
  Vec_Expl_1[1] = byte1;                
  Vec_Expl_1[2] = byte2;                
  Vec_Expl_1[3] = byte3;                

//                LSRB                    ;Divide first byte by 8
//                LSRB
//                LSRB
//                ORB     <Vec_Expl_1     ;OR with first byte
//                ANDB    #$07            ;AND with 7
//                STB     <Vec_Expl_Chans ;store in $C854
  Vec_Expl_Chans= Vec_Expl_Chans>>3;
  Vec_Expl_Chans = Vec_Expl_Chans | Vec_Expl_1[0];
  Vec_Expl_Chans = Vec_Expl_Chans & 7;
  
  
  
//                LDB     <Vec_Expl_1     ;Get first byte
//                ANDB    #$38            ;Mask off bits 3-5
//                STB     <Vec_Expl_ChanA ;store in $C853
  Vec_Expl_ChanA = Vec_Expl_ChanA & 0x38;
 
//                LDB     <Vec_Expl_1     ;Get first byte
//                ANDB    #$07            ;AND with 7
//                STB     <Vec_Expl_ChanB ;store in $C85D
 Vec_Expl_ChanB = Vec_Expl_ChanB &7;
 
//                LDB     #$02            ;Start with channel number 2
//                STB     <Vec_Expl_Chan
 Vec_Expl_Chan = 2;                
//                LDA     #$7F            ;Initialize time count
//                BRA     LF968
 Vec_Expl_Timer = 0x7f;      
 explosionStarted = 1;
 explosionFinished = 0;
  }
  else
  {
   // already init, now continue
//    LF95B:          LDA     <Vec_Expl_Timer
//                BEQ     LF9C9_RTS
   if (Vec_Expl_Timer == 0)
   { 
     stop();
     return;  
   }
//                SUBA    <Vec_Expl_4
//                BPL     LF968
//                CLRB
//                STB     <Vec_Expl_Timer
//                BRA     LF9CA

   Vec_Expl_Timer -= Vec_Expl_1[3];
   if (Vec_Expl_Timer<0)
   {

     Vec_Expl_Timer=0;
     b=0;
          
          
      {
      //LF9CA:          LDA     <Vec_Expl_Chans
      //                LDX     #Vec_Music_Wk_7
      //LF9CF:          TSTA                    ;Exit if all channels done
      //                BEQ     LF9DB_RTS
      //                LEAX    -1,X            ;Point to next register (8-10)
      //                LSRA
      //                BCC     LF9CF
      //                STB     ,X              ;Store noise value if chan in use
      //                BRA     LF9CF
      //LF9DB_RTS:      RTS
        a = Vec_Expl_Chans;
        int AmpReg = 7;
        while (a!=0)
        {
            AmpReg++;
            if ((a&1) ==0) 
            {
              a = a>>1;
              continue;
            }
            a = a>>1;
            v_writePSG_double_buffered(AmpReg, b);
        }
      }
      return;     
     
     
     
   }
  }
  

  
//LF968:          STA     <Vec_Expl_Timer
//                LSRA
//                LSRA
 a = Vec_Expl_Timer>>2; 
  
//                LDB     <Vec_Expl_ChanA
 b = Vec_Expl_ChanA;
//                BEQ     continueWithSoundChannels
 if (b == 0) goto continueWithSoundChannels;
//                STA     <Vec_Music_Wk_6
 v_writePSG_double_buffered(6, a);
 //               LDB     <Vec_Expl_2
 b = Vec_Expl_1[1]; // frequency swoop
//                BMI     LF97B
 if (b&0x80) goto LF97B;
//                BEQ     continueWithSoundChannels
 if (!b) goto continueWithSoundChannels;
//                TFR     A,B
 b = a;
//                COMB
 b = ~b; 
LF97B:          
//                STB     <Vec_Music_Wk_6
 v_writePSG_double_buffered(6, b);

continueWithSoundChannels:
//                LSRA
 a = a >>1;
//                CMPA    #$07
//                BLS     LF987
 if (a>7)
 {
//                CMPA    #$0F
//                BEQ     LF987
    if (a!= 0xf)
    {
//                INCA
      a++;
    }
 }
LF987:  
//                LDB     <Vec_Expl_3
 b = Vec_Expl_1[2]; // volume swoop
//                BMI     LF991
 if (b&0x80) goto LF991; // stay
//                BEQ     LF98F
 if (!b) goto LF98F;
//                EORA    #$0F
 a= a^0xf;
LF98F:
//                TFR     A,B
 b= a;                
LF991:
//                BSR     LF9CA
 {
//LF9CA:          LDA     <Vec_Expl_Chans
//                LDX     #Vec_Music_Wk_7
//LF9CF:          TSTA                    ;Exit if all channels done
//                BEQ     LF9DB_RTS
//                LEAX    -1,X            ;Point to next register (8-10)
//                LSRA
//                BCC     LF9CF
//                STB     ,X              ;Store noise value if chan in use
//                BRA     LF9CF
//LF9DB_RTS:      RTS
   a = Vec_Expl_Chans;
   int AmpReg = 7;
   while (a!=0)
   {
      AmpReg++;
      if ((a&1) ==0) 
      {
        a = a>>1;
        continue;
      }
      a = a>>1;
      v_writePSG_double_buffered(AmpReg, b);
   }
 }
 
                
//                LDB     <Vec_Expl_ChanB
 b = Vec_Expl_ChanB;
//                BEQ     LF9C2
 if (!b) goto LF9C2;
 
LF997:
//                LDA     <Vec_Expl_Chan  ;Get channel number
 a=Vec_Expl_Chan;                
//                DECA                    ;Decrement channel number
 a--;                
//                BPL     LF99E
 if (!(a&0x80)) goto LF99E;  
//                LDA     #$02            ;Reset to 2 if less than zero
 a= 2;                
LF99E:
//                STA     <Vec_Expl_Chan  ;Save channel number
 Vec_Expl_Chan = a;                
//                JSR     Bitmask_a       ;Get bit mask of the channel

 a = 1u << a;
//                BITA    <Vec_Expl_ChanB
//                BEQ     LF997           ;Go back if not in for $C85D
 if ((Vec_Expl_ChanB & a) == 0) goto LF997;            
//                LDB     <Vec_Expl_Chan
 b =  Vec_Expl_Chan;
//                ASLB                    ;Negative of channel number ; 2
//                NEGB
//                LDX     #Vec_Music_Wk_1 ;  (registers 1, 3, and 5)
//                LEAX    B,X

//                JSR     Random
 a = (unsigned char)rand();
//                ANDA    #$0F
 a = a & 0xf;
//                CMPA    #$05
//                BHI     LF9BC
 if (a>5) goto LF9BC;
//                ASLA
//                ADDA    #$05
 a = a<<1;
 a += 5;
LF9BC:
//                STA     ,X
// store a to coarse freq of channel in b
  v_writePSG_double_buffered(b*2+1, a);

//                LDA     <Vec_Random_Seed+1
 a = (unsigned char)rand();

 // store a to fine freq of channel in b
//                STA     1,X
  v_writePSG_double_buffered(b*2, a);
                
                
                
                
LF9C2:         
//                LDA     <Vec_Expl_1
//                COMA
 a = Vec_Expl_1[0];
 a = ~a;
//                ANDA    <Vec_Music_Wk_7
 a = a & v_readPSG_double_buffered(7);                
                
//                STA     <Vec_Music_Wk_7
 v_writePSG_double_buffered(7, a);
//LF9C9_RTS:      RTS
 return;
}


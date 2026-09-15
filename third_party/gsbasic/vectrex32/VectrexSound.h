/* 
 * File:   VectrexSound.h
 * Author: Bob
 *
 * Created on May 10, 2016, 7:57 PM
 */

#ifndef VECTREXSOUND_H
#define	VECTREXSOUND_H

#include "../Value.h"
class VMusic : public GSDataObject
{
public:

  VMusic() : adsr(nullptr), twang(nullptr), size(0), music(nullptr) {}
  VMusic(unsigned char *a, signed char *t, int s, unsigned char* m)
  : adsr(a), twang(t), size(s), music(m)
  {}

  unsigned char* adsr;  // pointer to 16 byte
  signed char* twang; // pointer to 16 byte
  int size;
  unsigned char* music;// arbitrary number of bytes
};

// it doesn't realy make sense to have more than one of 
// these at any time
// since vectrex can only play ONE music at any given time...
class VectrexMusicPlayer : public GSDataObject
{
public:

    VectrexMusicPlayer(VMusic *m);
    VectrexMusicPlayer(const Value &notes, unsigned char* a, signed char *t);
    VectrexMusicPlayer(int b0, int b1, int b2, int b3):byte0(b0),byte1(b1),byte2(b2),byte3(b3), 
                                                      isExplosion(1), is_Playing(0), Vec_Music_Flag(0), 
                                                      explosionStarted(0), explosionFinished(0), Vec_Expl_Flag(-1) 
                                                      {}

    virtual ~VectrexMusicPlayer() 
    {
      is_Playing = 0;
      Vec_Music_Flag = 0;
      if (music != 0)
      {
        if (music->music != 0)
          delete [] music->music;
        delete music;
      }
    }
    
    int play() 
    { 
      if (isExplosion == 0)
      {
        if ((is_Playing) && (Vec_Music_Flag!=0)) 
          Init_Music(); 
        return Vec_Music_Flag;
      }
      if (!is_Playing || explosionFinished)
        return 0;
      playExplosion();
      return explosionFinished;
    }
    
    
    void setPlaying(bool b)
    {
      if (b)
      {
        is_Playing = 1;
        explosionFinished = 0;
        explosionStarted = 0;
        Vec_Expl_Flag = -1;
        Vec_Music_Flag = 1;
      }
      else
      {
        stop();
      }
    }

    bool isPlaying()
    {
      return is_Playing;
    }
    void stop() 
    {
      is_Playing = 0;
      Vec_Expl_Flag = -1;
      Vec_Music_Flag = 0;
      explosionFinished = 1;
      explosionStarted = 0;
      v_noSound();
    }
    
private:
  void Init_Music();
  void playExplosion();

  VMusic *music=0;
  bool is_Playing = false;
  int Vec_Music_Flag;     // no sound
                          // 1 new sound
                          // -1 sound in progress
  int Vec_Music_Chan=0;
  unsigned char *Vec_Music_Ptr = 0;

  unsigned char Vec_ADSR_Timers[3];
  unsigned char Vec_Music_Twang_Count[3];

  signed char Vec_Music_Twang_Value[3];
  signed char Vec_Duration;
  int Vec_Music_Freq[3];
  
  unsigned char Vec_ADSR_Table[16];
  signed char Vec_Twang_Table[16];
  
  // explosion data follows

  bool isExplosion;  
  bool explosionStarted;
  bool explosionFinished;
  int byte0; // 00BCAbca channels used in effect ABC = noise, abc = sound
  int byte1; // frequency sweep up (0), down (1), stay (-1)
  int byte2; // volume sweep down (0), up (1), stay (-1)
  int byte3; // determines length of "explosion", the timer is decreased by this values, thus the higher the shorter the sound
  
  signed char Vec_Expl_Flag; // positive, explosion is running, negative explosion must be init
  unsigned char Vec_Expl_1[4];
  unsigned char Vec_Expl_Chans; // contains channels that are used (either sound/noise) 0000 0abc, bit values of a,b,c
  unsigned char Vec_Expl_ChanA; // contains NOISE channels used in explosion 00cb a000 , bit values of a,b,c
  unsigned char Vec_Expl_ChanB; // contains SOUND channels using in explosion 0000 0cba, bitvalues of a,b,c
  unsigned char Vec_Expl_Chan;  // loop counter for channels 0 or 1 or 2
  signed char Vec_Expl_Timer;   // timer of explosion sound, starts always with 0x7f, and is decreased each round by "byte3"
                                // devided by 4, this also provides the noise peroid for the PSG (Reg6) (that is frequency swwop)
                                // devided by 8, this also provides the volume for the PSG (Reg8,9,10) (that is volume swwop)
                                
};



#endif	/* VECTREXSOUND_H */


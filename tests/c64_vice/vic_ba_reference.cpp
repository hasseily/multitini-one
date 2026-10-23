// SPDX-License-Identifier: GPL-3.0-only
// Bounded sprite-only scheduler. Exact VICE tables, activation helpers and
// REU exception are extracted separately; drawing/badlines are not simulated.
#include <cstdint>
#include <cstdio>
#include <fstream>
struct {
    uint8_t regs[64]={};
    unsigned raster_line=0,raster_cycle=0,sprite_dma=0;
    struct {int mcbase=0,exp_flop=0;} sprite[8];
} vicii;
int cycles=63,cpu_ba_low=0;
int vicii_cycle();
#include "vice_ba_verbatim.inc"
int vicii_cycle() {
    vicii.raster_cycle++;
    if(vicii.raster_cycle==unsigned(cycles)){vicii.raster_cycle=0;vicii.raster_line++;}
    const auto *table=cycles==63?cycle_tab_pal:cycle_tab_ntsc;
    const auto &phi1=table[vicii.raster_cycle*2];
    if(phi1.flags&ChkSprDma)check_sprite_dma();
    // These fixtures disable DEN and finish before any sprite reaches MCBASE63.
    cpu_ba_low=(phi1.ba&BaSpr_M&vicii.sprite_dma)!=0;
    return cpu_ba_low;
}
int main() {
    unsigned mode,mask,y;
    std::ifstream input("case.hex");input>>std::hex>>mode>>mask>>y;
    if(!input||mode>1||mask>255||y<78||y>80)return 2;
    cycles=mode?65:63;vicii.regs[0x15]=mask;
    for(unsigned i=0;i<8;i++)vicii.regs[1+2*i]=y;
    puts("line,cycle,phase,ba,ba_reu");
    while(vicii.raster_line<82){
        const int reu_ba_low=vicii_cycle_reu();
        if(vicii.raster_line>=79&&vicii.raster_line<82)
            for(int phase:{15,28})
                printf("%u,%u,%d,%d,%d\n",vicii.raster_line,vicii.raster_cycle+1,phase,!cpu_ba_low,!reu_ba_low);
    }
    return ferror(stdout)?1:0;
}

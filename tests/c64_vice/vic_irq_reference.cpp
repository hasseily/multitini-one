// SPDX-License-Identifier: GPL-3.0-only
// Original test scaffold; extracted VICE bodies retain their own GPL-2.0-or-later notices.
#include <cstdint>
#include <cstdio>
using CLOCK=uint64_t;
CLOCK maincpu_clk=0;
int irq_pin=0;
struct { uint8_t regs[64]={}; unsigned raster_line=80,raster_irq_line=0,ysmooth=0; int raster_cycle=0,raster_irq_triggered=0,int_num=0; uint8_t irq_status=0; } vicii;
#define VICII_DEBUG_REGISTER(x) do {} while(0)
void maincpu_set_irq(int,int active){irq_pin=active;}
void maincpu_set_irq_clk(int,int active,CLOCK){irq_pin=active;}
#include "vice_irq_verbatim.inc"
void settle(){for(int i=0;i<3;i++){maincpu_clk++;reference_tick();}}
void sample(int stage){printf("%d,%u,%d,%d\n",stage,vicii.raster_line,irq_pin,vicii.irq_status&1);}
int main(){
 puts("stage,line,irq,irst");d011_store(0);d012_store(254);d01a_store(1);d019_store(15);settle();
 d012_store(80);settle();sample(0);
 d019_store(1);settle();sample(1);
 d012_store(81);settle();sample(2);
 d012_store(80);settle();sample(3);
 d019_store(1);settle();sample(4);
 d012_store(80);settle();sample(5); // rewriting the still-matching value cannot rearm
 d011_store(0x80);settle();sample(6);
 d011_store(0);settle();sample(7); // high comparator bit also rearms
 d01a_store(0);settle();sample(8); // mask gates the pin, not retained status
 d019_store(1);settle();sample(9);
 d01a_store(1);settle();sample(10);
 d012_store(81);settle();vicii.raster_line=81;settle();sample(11);
 return ferror(stdout) ? 1 : 0;
}

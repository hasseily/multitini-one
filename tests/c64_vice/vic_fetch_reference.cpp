// SPDX-License-Identifier: GPL-3.0-only
// Original test scaffold; extracted VICE bodies retain their own GPL-2.0-or-later notices.
#include <cstdint>
#include <cstdio>
#include <fstream>
struct {uint8_t regs[64]={},reg11_delay=0,gbuf=0,vbuf[40]={};unsigned vc=0,rc=0,vmli=0;int color_latency=1,vbank_phi1=0,vaddr_mask_phi1=0x3fff,vaddr_offset_phi1=0,vaddr_chargen_mask_phi1=0x7000,vaddr_chargen_value_phi1=0x1000;} vicii;
int last_address=0;
uint8_t fetch_phi1(int address){last_address=address;return 0xa5;}
#include "vice_fetch_verbatim.inc"
int main(){
 unsigned mode,bank,initial,cb,code,line,base;
 std::ifstream input("case.hex");input>>std::hex>>mode>>bank>>initial>>cb>>code>>line>>base;
 if(!input||mode>1||bank>1)return 2;
 vicii.vbank_phi1=bank?0:0x4000;
 vicii.vaddr_mask_phi1=0xffff;
 puts("cycle,address,vc,rc,code");
 for(int c=20;c<=34;c++){
  vicii.vc=base+c-16;vicii.rc=1;vicii.vmli=0;vicii.vbuf[0]=code;
  vicii.regs[0x18]=(c>=31)?(cb^8):cb;
  vicii.regs[0x11]=(c>=23&&c<=26)?(initial^0x20):initial;
  vicii.reg11_delay=(c>=24&&c<=27)?(initial^0x20):initial;
  vicii_fetch_graphics();
  printf("%d,%d,%u,%d,%u\n",c,last_address,base+c-16,1,code);
 }
 return ferror(stdout) ? 1 : 0;
}
